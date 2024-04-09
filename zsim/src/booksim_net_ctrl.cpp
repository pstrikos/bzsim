#ifdef _WITH_BOOKSIM_

#include "booksim_net_ctrl.h"
#include <map>
#include <string>
#include "event_recorder.h"
#include "tick_event.h"
#include "timing_event.h"
#include "zsim.h"


class BookSimAccEvent : public TimingEvent {
    private:
        BookSimNetwork* noc;
        bool write;
        Address addr;

    public:
        uint64_t sCycle;

        // BookSimAccEvent(DRAMSimMemory* _dram, bool _write, Address _addr, int32_t domain) :  TimingEvent(0, 0, domain), dram(_dram), write(_write), addr(_addr) {}
        BookSimAccEvent(BookSimNetwork* _noc, bool _write, Address _addr, int32_t domain) :  TimingEvent(0, 0, domain), noc(_noc), write(_write), addr(_addr) {}

        bool isWrite() const {
            return write;
        }

        Address getAddr() const {
            return addr;
        }

        void simulate(uint64_t startCycle) {
            sCycle = startCycle;
            //TODO: I also need source/destination
            noc->enqueue(this, startCycle); // noc has to get an enqueue function
        }

};

BookSimNetwork::BookSimNetwork(const char* _name, int _id, InterconnectInterface* _interface){
    nocIf = _interface;
    name = _name;
    id = _id;

    TickEvent<BookSimNetwork>* tickEv = new TickEvent<BookSimNetwork>(this, domain);
    tickEv->queue(0);  // start the sim at time 0
    tickEv->name = "booksimTickEvent0";

// //     curCycle = 0;
//     // minLatency = _minLatency;
//     // NOTE: this will alloc DRAM on the heap and not the glob_heap, make sure only one process ever handles this
//     // dramCore = getMemorySystemInstance(dramTechIni, dramSystemIni, outputDir, traceName, capacityMB);
//     // dramCore->setCPUClockSpeed(cpuFreqHz);

    booksim::TransactionCompleteCB *read_cb = new booksim::Callback<BookSimNetwork, void, unsigned, uint64_t, uint64_t>(this, &BookSimNetwork::noc_read_return_cb);
    booksim::TransactionCompleteCB *write_cb = new booksim::Callback<BookSimNetwork, void, unsigned, uint64_t, uint64_t>(this, &BookSimNetwork::noc_write_return_cb);
     
     nocIf->RegisterCallbacksInterface(read_cb, write_cb);

//     // domain = _domain;

//     // name = _name;
// // }

// void BookSimNetwork::initStats(AggregateStat* parentStat) {
//     // AggregateStat* memStats = new AggregateStat();
//     // memStats->init(name.c_str(), "Memory controller stats");
//     // profReads.init("rd", "Read requests"); memStats->append(&profReads);
//     // profWrites.init("wr", "Write requests"); memStats->append(&profWrites);
//     // localReqs.init("local", "Local requests"); memStats->append(&localReqs);
//     // remoteReqs.init("remote", "Remote requests"); memStats->append(&remoteReqs);
//     // profTotalRdLat.init("rdlat", "Total latency experienced by read requests"); memStats->append(&profTotalRdLat);
//     // profTotalWrLat.init("wrlat", "Total latency experienced by write requests"); memStats->append(&profTotalWrLat);
//     // parentStat->append(memStats);
// }
}
uint64_t BookSimNetwork::access(MemReq& req) {
     
        uint64_t respCycle = req.cycle;

        respCycle = req.cycle + 1; //TODO: replace with ZLL
        MemReq accReq = req;
        uint32_t nextLevelLat = parents[0]->access(accReq) - respCycle;
        respCycle += nextLevelLat;
        


    
        EventRecorder* evRec = zinfo->eventRecorders[req.srcId]; 
        TimingRecord tr = evRec->popRecord();
        // // TimingRecord wbAcc;
        // // wbAcc.clear();
        Address addr = req.lineAddr << (lineBits + 1);
        bool isWrite = -1;//(req.type == PUTX);
        BookSimAccEvent* nocEv = new (zinfo->eventRecorders[req.srcId]) BookSimAccEvent(this, isWrite, ++addr, domain);
        nocEv->setMinStartCycle(req.cycle);
        nocEv->setAddr(addr);
        nocEv->name = "nocEvent";
        
        
        TimingRecord noctr = {addr, req.cycle, respCycle, req.type, nocEv, nocEv};
        (noctr.startEvent)->addChild(tr.startEvent, evRec);
        evRec->pushRecord(noctr);

    

        // nocEv->setMinStartCycle(req.cycle);


        
        // Access may have generated another timing record. If *both* access
        // and wb have records, stitch them together
        // if (evRec->hasRecord()) {
        //     // Connect both events
        //     TimingRecord acc = evRec->popRecord();
        //     assert(wbAcc.reqCycle >= req.cycle);
        //     assert(acc.reqCycle >= req.cycle);
        //     DelayEvent* startEv = new (evRec) DelayEvent(0);
        //     DelayEvent* dWbEv = new (evRec) DelayEvent(wbAcc.reqCycle - req.cycle);
        //     DelayEvent* dAccEv = new (evRec) DelayEvent(acc.reqCycle - req.cycle);
        //     startEv->setMinStartCycle(req.cycle);
        //     dWbEv->setMinStartCycle(req.cycle);
        //     dAccEv->setMinStartCycle(req.cycle);
        //     startEv->addChild(dWbEv, evRec)->addChild(wbAcc.startEvent, evRec);
        //     startEv->addChild(dAccEv, evRec)->addChild(acc.startEvent, evRec);

        //     acc.reqCycle = req.cycle;
        //     acc.startEvent = startEv;
        //     // endEvent / endCycle stay the same; wbAcc's endEvent not connected
        //     evRec->pushRecord(acc);
        // } 
        // else{
        //     Address addr = req.lineAddr << lineBits;
            
        //     // Downstream should not care about endEvent for PUTs
        //     BookSimAccEvent* nocEv = new (zinfo->eventRecorders[req.srcId]) BookSimAccEvent(this, isWrite, addr, domain);
        //     nocEv->setMinStartCycle(req.cycle);
        //     TimingRecord tr = {addr, req.cycle, respCycle, req.type, nocEv, nocEv};
        //     evRec->pushRecord(tr);
        // }
        
    return respCycle;
}


uint32_t BookSimNetwork::tick(uint64_t cycle) {
    nocIf->Step();
    curCycle++;

    return 1;
}

void BookSimNetwork::enqueue(BookSimAccEvent* ev, uint64_t cycle) { 

    int _source = 0;
    int _dest = 1;
    int _size =5;
    nocIf->ManuallyGeneratePacket(_source, _dest, _size, cycle, ev->getAddr());
    inflightRequests.insert(std::pair<Address, BookSimAccEvent*>(ev->getAddr(), ev));
    ev->hold();
}

void BookSimNetwork::noc_read_return_cb(uint32_t id, uint64_t addr, uint64_t memCycle) {
    std::multimap<uint64_t, BookSimAccEvent*>::iterator it = inflightRequests.find(addr);
    assert((it != inflightRequests.end()));
    BookSimAccEvent* ev = it->second;

    uint32_t lat = curCycle + 1 - ev->sCycle;
    if (ev->isWrite()) {
        profWrites.inc();
        profTotalWrLat.inc(lat);
    } else {
        profReads.inc();
        profTotalRdLat.inc(lat);
    }

    ev->release();
    ev->done(curCycle+1);
    inflightRequests.erase(it);
}

// // void BookSimNetwork::DRAM_write_return_cb(uint32_t id, uint64_t addr, uint64_t memCycle) {
// //     //Same as read for now
// //     DRAM_read_return_cb(id, addr, memCycle);


// // #else //no dramsim, have the class fail when constructed

// // using std::string;

// // DRAMSimMemory::DRAMSimMemory(string& dramTechIni, string& dramSystemIni, string& outputDir, string& traceName,
// //         uint32_t capacityMB, uint64_t cpuFreqHz, uint32_t _minLatency, uint32_t _domain, const g_string& _name, int id)
// // {
// //     panic("Cannot use DRAMSimMemory, zsim was not compiled with DRAMSim");
// // }

// // void DRAMSimMemory::initStats(AggregateStat* parentStat) { panic("???"); }
// // uint64_t DRAMSimMemory::access(MemReq& req) { panic("???"); return 0; }
// // uint32_t DRAMSimMemory::tick(uint64_t cycle) { panic("???"); return 0; }
// // void DRAMSimMemory::enqueue(DRAMSimAccEvent* ev, uint64_t cycle) { panic("???"); }
// // void DRAMSimMemory::DRAM_read_return_cb(uint32_t id, uint64_t addr, uint64_t memCycle) { panic("???"); }
// // void DRAMSimMemory::DRAM_write_return_cb(uint32_t id, uint64_t addr, uint64_t memCycle) { panic("???"); }

// #endif

#endif