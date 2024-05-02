#ifndef BOOKSIM_NET_CTRL_H_
#define BOOKSIM_NET_CTRL_H_

#ifdef _WITH_BOOKSIM_
#include <map>
#include <cmath>
#include <string>
#include "g_std/g_string.h"
#include "memory_hierarchy.h"
#include "pad.h"
#include "stats.h"
#include <iostream>
#include "interconnect_interface.hpp"
#include "coord.h"

class SplitAddrMemory;
class BookSimAccEvent;

class BookSimNetwork : public BaseCache { 
    private:
        InterconnectInterface* nocIf;
        g_string name;
        int id;
        bool isLocal;
        uint32_t minLatency; //TODO: is this event used?
        uint32_t domain;
        g_vector<MemObject*> parents;
        g_vector<BaseCache*> children;
        int numChildren;
        bool isLlnoc; // true if the noc interface is connected to LLC

        std::unordered_map<uint64_t,BookSimAccEvent*> inflightRequests;

        uint64_t nocCurCycle; //processor cycle, used in callbacks

        // int hopLatency = 3;
        // int flitsPerPacket = 5;
        int meshDim;       

        int nocFreq, cpuFreq, nocSpeedup;
        int nocCount, cpuCount; 

        int packetSize;
        int hopDelay;

        uint32_t zsimPhaseLength;
        uint32_t namecnt; 

        // R/W stats
        PAD();
        lock_t  netLockAcc;
        lock_t  netLockInv;
        lock_t  cb_lock;
        Counter profReads;
        Counter profWrites;
        Counter localReqs, remoteReqs;
        Counter profTotalRdLat;
        Counter profTotalWrLat;
#ifdef _SANITY_CHECK_
        Counter nocGETS, nocGETX, nocPUTS, nocPUTX;
#endif
        PAD();

    public:
        BookSimNetwork(const char* _name, int _id, InterconnectInterface* _interface, int _cpuClk);

        void enqueueTickEvent();
        const char* getName() {return name.c_str();}
        int getMemId() {
            return id;
        }
        bool setIsLocal(bool val) {
            isLocal=val;
            return 0;
        }
        bool getIsLocal() {
            return isLocal;
        }

        void initStats(AggregateStat* parentStat);

        // Record accesses
        uint64_t access(MemReq& req);
        uint64_t processAccess(Address lineAddr, uint32_t lineId, AccessType type, uint64_t cycle, uint32_t srcId, uint32_t flags,
									 uint64_t previous_instructions, uint32_t coreId, uint32_t flagsTraces);

        // Event-driven simulation (phase 2)
        uint32_t tick(uint64_t cycle);
        void enqueue(BookSimAccEvent* ev, uint64_t cycle);
    
        //parentless for now
        void setParents(uint32_t _childId, const g_vector<MemObject*>& _parents, zsimNetwork* network){
            parents.resize(_parents.size());
            for (uint32_t p = 0; p < _parents.size(); p++) {
                parents[p] = _parents[p];
            }
        }
        bool hasParents(){return !parents.empty();}
        
        void setChildren(const g_vector<BaseCache*>& _children, zsimNetwork* network);
        inline int getNumChildren() {return numChildren;}
        uint64_t invalidate(const InvReq& req);

        void DisplayStats(){nocIf->DisplayOverallStats();}

        void setLlnoc(bool _isLlnoc){isLlnoc = _isLlnoc;}

        void setGrandChildren(const g_vector<BaseCache*>& children) {panic("Should never be called");};
        void incrNumGrandChildren(const int numGrandChildren){panic("Should never be called");};
        int getNumParents() {panic("Should never be called");};
        g_vector<MemObject*> getParents() {panic("Should never be called");};
        void setCoord(const coordinates<int> coord) {panic("Should never be called");};
        coordinates<int> getCoord(){panic("Should never be called");};
        coordinates<int> getCoord(MemReq& req){panic("Should never be called");};

    private:
        void startAccess(MemReq& req);
        void endAccess(MemReq& req);

        void noc_read_return_cb(uint32_t id, uint64_t pid, uint64_t latency);
        void noc_write_return_cb(uint32_t id, uint64_t pid, uint64_t latency);
};

#endif  

#endif