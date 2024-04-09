#ifdef _WITH_BOOKSIM_

#ifndef BOOKSIM_NET_CTRL_H_
#define BOOKSIM_NET_CTRL_H_
#include <map>
#include <string>
#include "g_std/g_string.h"
#include "memory_hierarchy.h"
#include "pad.h"
#include "stats.h"
#include <iostream>
#include "interconnect_interface.hpp"

// namespace DRAMSim {
//     class MultiChannelMemorySystem;
// };

class BookSimAccEvent;

class BookSimNetwork : public MemObject { 
    private:
        InterconnectInterface* nocIf;
        g_string name;
        int id;
        bool isLocal;
        uint32_t minLatency = 2; //TODO: zll
        uint32_t domain;
        g_vector<MemObject*> parents;

        //Booksim::NetworkInterface ??

        std::multimap<uint64_t, BookSimAccEvent*> inflightRequests;

        uint64_t curCycle; //processor cycle, used in callbacks

        // R/W stats
        PAD();
        Counter profReads;
        Counter profWrites;
        Counter localReqs, remoteReqs;
        Counter profTotalRdLat;
        Counter profTotalWrLat;
        PAD();

    public:
        BookSimNetwork(const char* _name, int _id, InterconnectInterface* _interface);
        // BookSimNetwork(std::string& dramTechIni, std::string& dramSystemIni, std::string& outputDir, std::string& traceName, uint32_t capacityMB,
                // uint64_t cpuFreqHz,  uint32_t _minLatency, uint32_t _domain, const g_string& _name, int id);
        // BookSimNetwork();

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

        void initStats(AggregateStat* parentStat){
            
        };

        // Record accesses
        uint64_t access(MemReq& req);
        uint64_t processAccess(Address lineAddr, uint32_t lineId, AccessType type, uint64_t cycle, uint32_t srcId, uint32_t flags,
									 uint64_t previous_instructions, uint32_t coreId, uint32_t flagsTraces);

        // Event-driven simulation (phase 2)
        uint32_t tick(uint64_t cycle);
        void enqueue(BookSimAccEvent* ev, uint64_t cycle);
    
        //parentless for now
        void setParents(uint32_t _childId, const g_vector<MemObject*>& _parents, Network* network){
            parents.resize(_parents.size());
            // parentRTTs.resize(_parents.size());
            for (uint32_t p = 0; p < _parents.size(); p++) {
                parents[p] = _parents[p];
                // parentRTTs[p] = (network)? network->getRTT(name, parents[p]->getName()) : 0;
            }
        }
        bool hasParents(){return !parents.empty();}
        
    private:
        void noc_read_return_cb(uint32_t id, uint64_t addr, uint64_t returnCycle);
        void noc_write_return_cb(uint32_t id, uint64_t addr, uint64_t returnCycle){};
};

#endif  

#endif