#ifdef _WITH_BOOKSIM_
#include "booksim_net_ctrl.h"
#include <map>
#include <string>
#include "event_recorder.h"
#include "tick_event.h"
#include "timing_event.h"
#include "coord.h"

class SplitAddrMemory;

class BookSimAccEvent : public TimingEvent {
    private:
        BookSimNetwork* noc;
        bool write;
        Address addr;
        doubleCoordinates<int> coord;

    public:
        uint64_t sCycle;

        explicit BookSimAccEvent(BookSimNetwork* _noc, bool _write, Address _addr, int32_t domain, bool isInval = false) :  TimingEvent(0, 0, domain, isInval), noc(_noc), write(_write), addr(_addr) {}

        bool isWrite() const {
            return write;
        }

        Address getAddr() const {
            return addr;
        }

        doubleCoordinates<int> getCoord() const {
            return coord;
        }

        void setCoord(doubleCoordinates<int> coordinates){
            coord = coordinates;
        }

        void simulate(uint64_t startCycle) {
            sCycle = startCycle;
            noc->enqueue(this, startCycle); // noc has to get an enqueue function
        }

};

BookSimNetwork::BookSimNetwork(const char* _name, int _id, InterconnectInterface* _interface, int _cpuFreq){
    nocIf = _interface;
    cpuFreq = _cpuFreq;
    nocFreq = nocIf->getNocFrequency();
    nocSpeedup = nocFreq/cpuFreq; // int on purpose
    hopDelay = nocIf->getHopDelay();
    packetSize = nocIf->getPacketSize();
    name = _name;
    id = _id;
    numChildren = 0;
    meshDim = gX;
    isLlnoc = false;

    futex_init(&netLockAcc);
    futex_init(&netLockInv);
    futex_init(&cb_lock);

    booksim::TransactionCompleteCB *read_cb = new booksim::Callback<BookSimNetwork, void, unsigned, uint64_t, uint64_t>(this, &BookSimNetwork::noc_read_return_cb);
    booksim::TransactionCompleteCB *write_cb = new booksim::Callback<BookSimNetwork, void, unsigned, uint64_t, uint64_t>(this, &BookSimNetwork::noc_write_return_cb);
     
     nocIf->RegisterCallbacksInterface(read_cb, write_cb, this);

}

void BookSimNetwork::enqueueTickEvent(){
    TickEvent<BookSimNetwork>* tickEv = new TickEvent<BookSimNetwork>(this, domain);
    tickEv->queue(0);  // start the sim at time 0
}

void BookSimNetwork::initStats(AggregateStat* parentStat) {
    AggregateStat* nocStats = new AggregateStat();
    nocStats->init(name.c_str(), "NoC stats");
    profReads.init("rd", "Read requests"); nocStats->append(&profReads);
    profWrites.init("wr", "Write requests"); nocStats->append(&profWrites);
    localReqs.init("local", "Local requests"); nocStats->append(&localReqs);
    remoteReqs.init("remote", "Remote requests"); nocStats->append(&remoteReqs);
    profTotalRdLat.init("rdlat", "Total latency experienced by read requests"); nocStats->append(&profTotalRdLat);
    profTotalWrLat.init("wrlat", "Total latency experienced by write requests"); nocStats->append(&profTotalWrLat);
#ifdef _SANITY_CHECK_
    nocGETS.init("nocGETS", "nocGETS"); nocStats->append(&nocGETS);
    nocGETX.init("nocGETX", "nocGETX"); nocStats->append(&nocGETX);
    nocPUTS.init("nocPUTS", "nocPUTS"); nocStats->append(&nocPUTS);
    nocPUTX.init("nocPUTX", "nocPUTX"); nocStats->append(&nocPUTX);
#endif
    parentStat->append(nocStats);
}

void BookSimNetwork::startAccess(MemReq& req){
    if (req.childLock)
    {
        futex_unlock(req.childLock);
    }

    futex_lock(&netLockAcc);
    futex_lock(&netLockInv);
}

void BookSimNetwork::endAccess(MemReq& req){
    if (req.childLock)
    {
        futex_lock(req.childLock);
    }
    futex_unlock(&netLockInv);
    futex_unlock(&netLockAcc);
}

uint64_t BookSimNetwork::access(MemReq& req) {
        
        startAccess(req);
 #ifdef _SANITY_CHECK_   
        switch (req.type) {
        case PUTS:
            nocPUTS.inc();
            break;
        case PUTX:
            nocPUTX.inc();
            break;
        case GETS:
            nocGETS.inc();
            break;
        case GETX:
            nocGETX.inc();
            break;
        default: 
            panic("!?");
    }
#endif

        // The request (req) that is passed, contains an 'address', based on which, we do the following:
        // 1) translate it in network address. If there are multiple MCs, the splitter contacts the correct one
        // 2) create a doubleCoord that will later be stored in booksimAccEv->coord
        // 3) use them to calculate zll
        coordinates<int> src = req.nocCoord;    
        coordinates<int> dst = parents[0]->getCoord(req);
        uint64_t respCycle = req.cycle;

        AccessType type = req.type;

        EventRecorder* evRec = zinfo->eventRecorders[req.srcId]; 

        TimingRecord trInv;
        trInv.clear();
        if(evRec->hasRecord() && isLlnoc){
            trInv = evRec->popRecord();
            assert((trInv.startEvent)->getIsInval());
        }

        // zll needs to be devided by nocSpeedup
        // Although in reality, the NoC calculates zll to be C cycles, zsim might operate in a different freq,
        // So from its point of view, the packet will need C/N cycles if for example the CPU is N times slower
        // nocSpeedup = nocFreq/cpuFreq
        // TODO: Remove the addition of "+2" to account for the cycles added by booksim to inject to the first R
        int hops = abs(dst.x-src.x) + abs(dst.y-src.y);
        int zll =(hops + 1)*hopDelay + packetSize-1 + 2;
        zll = zll*cpuFreq/nocFreq;

        respCycle = req.cycle + zll;

        MemReq accReq = req;
        accReq.cycle = respCycle;
        accReq.nocReq = true;
        accReq.nocChildId = req.srcId;
        accReq.childLock = &netLockInv;

        assert(netLockAcc);
        uint64_t parLat = parents[0]->access(accReq);
        assert(netLockAcc);

        uint32_t nextLevelLat = parLat - respCycle;
        
        // this means cache skipped the access
        if(nextLevelLat == 0){
            if(trInv.isValid()){
                evRec->pushRecord(trInv);
            }
            endAccess(req);
            return req.cycle;
        }

        TimingRecord tr;
        tr = evRec->popRecord();
    
        Address addr = req.lineAddr;
        bool isWrite = (type == PUTX);


        doubleCoordinates<int> coordT = {src,dst};
        doubleCoordinates<int> coordR = {dst,src};

        // First create a Transmit and a Receive event for accessing the memory.
        // The two events will be put before and after the DRAMSim event.
        BookSimAccEvent* nocEvT = new (zinfo->eventRecorders[req.srcId]) BookSimAccEvent(this, isWrite, addr, domain);

        nocEvT->setMinStartCycle(req.cycle);
        nocEvT->setCoord(coordT); 
        nocEvT->setZll(zll);
        respCycle += nextLevelLat; 
        BookSimAccEvent* nocEvR = new (zinfo->eventRecorders[req.srcId]) BookSimAccEvent(this, isWrite, addr, domain);
        
        nocEvR->setMinStartCycle(respCycle);
        nocEvR->setCoord(coordR);
        nocEvR->setZll(zll);

        // Then create two more events for simulating the L2-L3 access.
        // The difference now is that before we had a simulation for the DRAM, but now we have just a latency for L3.
        // So this time we need to set the postdelay of the first transmit event to delay it for the amount of time
        // that L2 would normally need.
        // Also, we need to take into account the difference between the CPU's and the NoC's clock.
        // If the NoC is N time faster that the CPU, then the packet would be N times faster to arrive.
        // This would normally be calculated in the NoC as a zll, so now we just devide the delay by the nocSpeedup  
        if (tr.startEvent != nullptr){
            if(tr.startEvent->getIsInval()){
                // this means that have a single invalidation request waiting in the recorders.
                // The two noc requests should sandwitch that request
                
                // nocEvR->name += "IsInval"; 
                // nocEvT->name += "IsInval"; 
                nocEvR->setIsInval(true);
                nocEvT->setIsInval(true);


                TimingEvent *firstEv = tr.startEvent;
                TimingEvent *lastEv = tr.startEvent->getChildLeftDescendant();

                nocEvT->setPostDelay(firstEv->getMinStartCycle() - req.cycle - zll);
                nocEvT->addChild(firstEv,evRec);
                lastEv->addChild(nocEvR,evRec);
                
                lastEv->setPostDelay(nocEvR->getMinStartCycle() - lastEv->getMinStartCycle() - lastEv->getZll());
            }else{
                TimingEvent *firstEv = tr.startEvent;
                TimingEvent *lastEv = tr.startEvent;
                
                if(firstEv->getNumChildren() > 0){
                    // calculate the time between the cycle the request arrives in L3 (req.cycle)
                    // and the cycle the request arrives at L2 (firstEv->getMinStartCycle).
                    // This is how much we need to delay the event simulating the delay of the cache.
                    // From that, sub zll since when we enqueue the next request is based on
                    // the doneCycle + postDelay and doneCycle will hopefully be minCylce + zll
                    nocEvT->setPostDelay(firstEv->getMinStartCycle()-req.cycle-zll);
                }
                while(lastEv->getNumChildren() > 0){
                    if (lastEv->getNumChildren() == 1){
                        lastEv = lastEv->getChild();
                    }
                    else {
                        lastEv = lastEv->getSelectedChild(1);
                    }
                }

                (nocEvT)->addChild(firstEv, evRec);
                lastEv->addChild(nocEvR, evRec);
            }
        }
        else {
            nocEvT->setPostDelay(nextLevelLat);
            (nocEvT)->addChild(nocEvR, evRec);
        }

        // if this is the llnoc, by now I have created the chain with the dramsim access
        // So, its time to check if there is an invalidation chain waiting in trInv, and if yes, to connect them
        // For now I'm only working assuming there is a single invalidation event waiting in the queue
        if(trInv.isValid()){
            TimingEvent *firstEvInv = trInv.startEvent;
            TimingEvent *lastEvInv = trInv.startEvent->getChildLeftDescendant();
            
            lastEvInv->addChild(nocEvT,evRec);
            assert(lastEvInv->getMinStartCycle() + lastEvInv->getZll() == nocEvT->getMinStartCycle());

            TimingRecord noctr = {addr, req.cycle, respCycle + zll, type, firstEvInv, firstEvInv};
            evRec->pushRecord(noctr);
        }
        else{
            TimingRecord noctr = {addr, req.cycle, respCycle + zll, type, nocEvT, nocEvT};
            evRec->pushRecord(noctr);
        }

        respCycle += zll; // count again zll for the trip back
        endAccess(req);

        return respCycle;
}


uint32_t BookSimNetwork::tick(uint64_t cycle) {

    nocCurCycle = nocIf->getNocCurCycle();
    if (cpuFreq == nocFreq){
	    nocIf->Step();
        nocIf->setNocCurCycle(++nocCurCycle);
        return 1; 
    }

    nocCount += nocFreq;

    while (cpuCount < nocCount){
        cpuCount += cpuFreq;
        nocIf->Step();
        nocIf->setNocCurCycle(++nocCurCycle);
    }

    if (cpuCount == nocCount){
        cpuCount = 0;
        nocCount = 0;
    }

    return 1;
}

void BookSimNetwork::enqueue(BookSimAccEvent* ev, uint64_t cycle) { 
    doubleCoordinates<int> coord = ev->getCoord();
    int _source = meshDim*(coord.src.x) + coord.src.y;
    int _dest = meshDim*(coord.dest.x) + coord.dest.y;
    int curPid = nocIf->ManuallyGeneratePacket(_source, _dest, packetSize, -1, ev->getAddr(), this);
    inflightRequests.insert(std::pair<int,BookSimAccEvent*>(curPid, ev));
    ev->hold();
}

void BookSimNetwork::setChildren(const g_vector<BaseCache*>& _children, zsimNetwork* network){
            children.resize(_children.size());
            for (uint32_t c = 0; c < _children.size(); c++) {
                children[c] = _children[c];
                numChildren++;
            }
        };

uint64_t BookSimNetwork::invalidate(const InvReq& req){

    futex_lock(&netLockInv);
    uint64_t respCycle = req.cycle;

    coordinates<int> src = parents[0]->getCoord();
    coordinates<int> dst = children[req.nocChildId]->getCoord();
    doubleCoordinates<int> coordInvT = {src,dst};
    doubleCoordinates<int> coordInvR = {dst,src};
    int hops = abs(dst.x-src.x) + abs(dst.y-src.y);
    int zll =(hops + 1)*hopDelay + packetSize-1 + 2;
    zll = zll*cpuFreq/nocFreq;


    BookSimAccEvent* nocEvInvT = new (zinfo->eventRecorders[req.srcId]) BookSimAccEvent(this, 0, req.lineAddr, 0, true);
    
    nocEvInvT->setMinStartCycle(respCycle); // the packet is injected when the nocs parent calls the inval function
    nocEvInvT->setCoord(coordInvT);
    nocEvInvT->setZll(zll);

    respCycle += zll;

    // InvType type = req.type;
    InvReq request = req;
    request.cycle = respCycle;
    request.nocReq = false;

    // calculate the time the invalidation request needs to propagate through all the cache levels starting at request.cycle
    uint64_t childLat = children[req.nocChildId]->invalidate(request);
    uint32_t prevLevelLat = childLat - respCycle; 

    EventRecorder* evRec = zinfo->eventRecorders[req.srcId]; 
    TimingRecord tr = evRec->popRecord();

    // increase again the time and create the R event at the time the cache invalidation finishes
    respCycle += prevLevelLat;

    BookSimAccEvent* nocEvInvR = new (zinfo->eventRecorders[req.srcId]) BookSimAccEvent(this, 0, request.lineAddr, 0, true);
    
    nocEvInvR->setMinStartCycle(respCycle); // the packet is injected when the nocs parent calls the inval function
    nocEvInvR->setCoord(coordInvR);
    nocEvInvR->setZll(zll);

    respCycle += zll; 

    nocEvInvT->addChild(nocEvInvR,evRec);
    nocEvInvT->setPostDelay(prevLevelLat);


    TimingRecord noctr = {request.lineAddr, req.cycle, respCycle, GETX, nocEvInvT, nocEvInvT}; // put GETX to stop it from complaining 

    // tr might contain another invalidation event introduced by the same noc for a different child
    if(tr.startEvent != nullptr){
        assert(tr.startEvent->getNumChildren() > 0); // if there is a event here, it should be an invalidation/delay event, so it has to have children
        if(tr.startEvent->getNumChildren() == 1){ // this is just the second event 
            DelayEvent* rootDelayEv = new (evRec) DelayEvent(0);
            DelayEvent* syncDelayEv = new (evRec) DelayEvent(0);
            TimingEvent* prevNocEvInvR = tr.startEvent->getChildLeftDescendant();
            
            rootDelayEv->setIsInval(true);
            syncDelayEv->setIsInval(true);


            rootDelayEv->setMinStartCycle(req.cycle);

            
            // make both chains children of the root event, and parents of the sync event            
            rootDelayEv->addChild(tr.startEvent, evRec);
            rootDelayEv->addChild(nocEvInvT, evRec);
            nocEvInvR    ->addChild(syncDelayEv, evRec);
            prevNocEvInvR->addChild(syncDelayEv, evRec);


            rootDelayEv->setMinStartCycle(req.cycle);

            noctr.startEvent = rootDelayEv;
            noctr.endEvent = rootDelayEv;
        }
        else{
            // there are already a root and sync delay events waiting 
            assert(tr.startEvent->getMinStartCycle() == nocEvInvT->getMinStartCycle()); // all invalidations start at the same cycle
            TimingEvent* prevSyncDelayEvent = tr.startEvent->getChildLeftDescendant();

            tr.startEvent->addChild(nocEvInvT, evRec); // add the T invalidation event to the root event
            nocEvInvR->addChild(prevSyncDelayEvent, evRec); // find the sync delay event and set it ass a child of the R invalidation event

            noctr.startEvent = tr.startEvent;
            noctr.endEvent = tr.startEvent;
        }
    }

    evRec->pushRecord(noctr);
    futex_unlock(&netLockInv); 
    return respCycle;

}

void BookSimNetwork::noc_read_return_cb(uint32_t id, uint64_t pid, uint64_t latency) {
    futex_lock(&cb_lock);

    int curCycle = (nocIf->getNocCurCycle())*cpuFreq/nocFreq;  
    std::unordered_map<int, BookSimAccEvent*>::iterator it = inflightRequests.find(pid);

    if(it == inflightRequests.end()){
        std::cout << "BookSimNetwork " << this->name << " received a callback for an address that doesn't exist" << std::endl;
        assert(0);
    }
    BookSimAccEvent* ev = it->second;  
    uint32_t lat = curCycle - ev->sCycle;
    
    assert((uint32_t) ev->getZll() <= lat);

    if (ev->isWrite()) {
        profWrites.inc();
        profTotalWrLat.inc(lat);
    } else {
        profReads.inc();
        profTotalRdLat.inc(lat);
    }

    futex_unlock(&cb_lock);

    ev->release();
    ev->done(curCycle);
    inflightRequests.erase(it);
}

void BookSimNetwork::noc_write_return_cb(uint32_t id, uint64_t pid, uint64_t latency) {
    noc_read_return_cb(id, pid, latency);
}


#endif
