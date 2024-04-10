#ifdef _WITH_BOOKSIM_

#include "booksim_net_ctrl.h"
#include <map>
#include <string>
#include "event_recorder.h"
#include "tick_event.h"
#include "timing_event.h"
#include "coord.h"


class BookSimAccEvent : public TimingEvent {
    private:
        BookSimNetwork* noc;
        bool write;
        Address addr;
        doubleCoordinates<int> coord;

    public:
        std::string mytype(){return "booksimevent";};
        uint64_t sCycle;

        BookSimAccEvent(BookSimNetwork* _noc, bool _write, Address _addr, int32_t domain) :  TimingEvent(0, 0, domain), noc(_noc), write(_write), addr(_addr) {}

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
    name = _name;
    id = _id;
    numChildren = 0;

    booksim::TransactionCompleteCB *read_cb = new booksim::Callback<BookSimNetwork, void, unsigned, uint64_t, uint64_t>(this, &BookSimNetwork::noc_read_return_cb);
    booksim::TransactionCompleteCB *write_cb = new booksim::Callback<BookSimNetwork, void, unsigned, uint64_t, uint64_t>(this, &BookSimNetwork::noc_write_return_cb);
     
     nocIf->RegisterCallbacksInterface(read_cb, write_cb);

}

void BookSimNetwork::enqueueTickEvent(){
    TickEvent<BookSimNetwork>* tickEv = new TickEvent<BookSimNetwork>(this, domain);
    tickEv->name = "booksimTickEvent0";
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
    parentStat->append(nocStats);
}
int namecnt = 0;
uint64_t BookSimNetwork::access(MemReq& req) {
        uint64_t respCycle = req.cycle;
        AccessType type = req.type;
        // The request (req) that is passed, contains an 'address'.
        // Here, we will get that address and do the following:
        // 1) translate it in network address
        // 2) create a doubleCoord that will later be stored in booksimAccEv->coord
        // 3) use them to calculate zll
        
        // TODO: info about source and destination must be stored in the request
        // also, zll maybe can be stored also in the request,
        // For now, hardcode zll and `src`, `dst` values. Later they will be calculated dynamically,
        // as described above.
        
        coordinates<int> src = req.nocCoord;
        coordinates<int> dst = parents[0]->getCoord();
        doubleCoordinates<int> coordT = {src,dst};
        doubleCoordinates<int> coordR = {dst,src};

        int zll = (abs(dst.x-src.x) + abs(dst.y-src.y) + 1)*(hopLatency)+2 + (flitsPerPacket-1);

        respCycle = req.cycle + zll;

        MemReq accReq = req;
        accReq.cycle = respCycle;
        accReq.nocReq = true;
        accReq.nocChildId = req.srcId;

        uint64_t parLat = parents[0]->access(accReq);
        uint32_t nextLevelLat = parLat - respCycle;
        
        // this means cache skipped the access
        if(nextLevelLat == 0){
            return req.cycle;
        }

        EventRecorder* evRec = zinfo->eventRecorders[req.srcId]; 
        TimingRecord tr = evRec->popRecord();
    
        Address addr = req.lineAddr << (lineBits + 1);
        bool isWrite = (type == PUTX);

        BookSimAccEvent* nocEvT = new (zinfo->eventRecorders[req.srcId]) BookSimAccEvent(this, isWrite, ++addr, domain);
        nocEvT->setMinStartCycle(req.cycle);
        nocEvT->setAddr(addr);
        nocEvT->name = "nocEventTransmit" + std::to_string(namecnt);
        nocEvT->setCoord(coordT); 

        TimingRecord noctr = {addr, req.cycle, respCycle, req.type, nocEvT, nocEvT};
        respCycle += nextLevelLat; 
        
        BookSimAccEvent* nocEvR = new (zinfo->eventRecorders[req.srcId]) BookSimAccEvent(this, isWrite, ++addr, domain);
        nocEvR->setMinStartCycle(respCycle);
        nocEvR->setAddr(addr);
        nocEvR->name = "nocEventRespond" + std::to_string(namecnt++); 
        nocEvR->setCoord(coordR);

        DelayEvent* dAccEv = new (zinfo->eventRecorders[req.srcId]) DelayEvent(respCycle);
        dAccEv->setMinStartCycle(respCycle);

        
        if (tr.startEvent != nullptr){
            TimingEvent *firstEv = tr.startEvent;
            TimingEvent *lastEv = tr.startEvent;
            
            if(firstEv->getNumChildren() > 0){
                nocEvT->setPostDelay((firstEv->getMinStartCycle()-req.cycle)/nocSpeedup);
            }

            while(lastEv->getNumChildren() == 1){
                lastEv = lastEv->getChild();
            }
            (noctr.startEvent)->addChild(firstEv, evRec);
            lastEv->addChild(nocEvR, evRec);
            evRec->pushRecord(noctr);
        }
        else {
            nocEvT->setPostDelay((nextLevelLat+zll)/nocSpeedup);
            (noctr.startEvent)->addChild(nocEvR, evRec);
            evRec->pushRecord(noctr);
        }

        respCycle += zll; // count again zll for the trip back
        
        
    return respCycle;
}


uint32_t BookSimNetwork::tick(uint64_t cycle) {

    curCycle = nocIf->getCurCycle();

    if (cpuFreq == nocFreq){
	    nocIf->Step();
        nocIf->setCurCycle(++curCycle);
        return 1; 
    }

    nocCount += nocFreq;

    while (cpuCount < nocCount){
        cpuCount += cpuFreq;
        nocIf->Step();
    }

    if (cpuCount == nocCount){
        cpuCount = 0;
        nocCount = 0;
    }

    nocIf->setCurCycle(++curCycle);
    return 1;
}

void BookSimNetwork::enqueue(BookSimAccEvent* ev, uint64_t cycle) { 

    doubleCoordinates<int> coord = ev->getCoord();
    int _source = meshDim*(coord.src.x) + coord.src.y;
    int _dest = meshDim*(coord.dest.x) + coord.dest.y;
    int _size = flitsPerPacket;
    nocIf->ManuallyGeneratePacket(_source, _dest, _size, -1, ev->getAddr());
    inflightRequests.insert(std::pair<Address, BookSimAccEvent*>(ev->getAddr(), ev));
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
    InvReq request = req;
    request.nocReq = false;
    int64_t respCycle = children[req.nocChildId]->invalidate(request);
    return respCycle;
}
// Remember here that DRAMSimMemory's callback gets *curCycle* from DRAMSim2, not *latency*
// That number is not used anyway, except for debugging purposes.
void BookSimNetwork::noc_read_return_cb(uint32_t id, uint64_t addr, uint64_t latency) {
    std::multimap<uint64_t, BookSimAccEvent*>::iterator it = inflightRequests.find(addr);
    if(it == inflightRequests.end()){
        return;
    }
    BookSimAccEvent* ev = it->second;
    curCycle = nocIf->getCurCycle();
    uint32_t lat = curCycle - ev->sCycle;
    if (ev->isWrite()) {
        profWrites.inc();
        profTotalWrLat.inc(lat);
    } else {
        profReads.inc();
        profTotalRdLat.inc(lat);
    }

    ev->release();
    ev->done(curCycle);
    inflightRequests.erase(it);
}

void BookSimNetwork::noc_write_return_cb(uint32_t id, uint64_t addr, uint64_t latency) {
    noc_read_return_cb(id, addr, latency);
}

#endif