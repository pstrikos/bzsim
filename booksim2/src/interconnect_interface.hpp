// Copyright (c) 2009-2013, Tor M. Aamodt, Dongdong Li, Ali Bakhoda
// The University of British Columbia
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// Redistributions of source code must retain the above copyright notice, this
// list of conditions and the following disclaimer.
// Redistributions in binary form must reproduce the above copyright notice, this
// list of conditions and the following disclaimer in the documentation and/or
// other materials provided with the distribution.
// Neither the name of The University of British Columbia nor the names of its
// contributors may be used to endorse or promote products derived from this
// software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
// ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
// FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
// DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
// CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
// OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#ifndef _INTERCONNECT_INTERFACE_HPP_
#define _INTERCONNECT_INTERFACE_HPP_

#include <vector>
#include <queue>
#include <iostream>
#include <map>
#include "globals.hpp"
#include "callback.hpp"
using namespace std;


// Do not use #include since it will not compile in icnt_wrapper or change the makefile to make it
class Flit;
class TrafficManager;
class IntersimConfig;
class Network;
class Stats;
class BookSimConfig;
class BookSimNetwork;

typedef booksim::CallbackBase<void,unsigned,uint64_t,uint64_t> Callback_t;

class InterconnectInterface {
public:
  InterconnectInterface();
  virtual ~InterconnectInterface();
  static InterconnectInterface* New(const char* const config_file);
  void CreateInterconnect();
  
  int ManuallyGeneratePacket(int source, int dest, int size, simTime ctime, uint64_t addr, BookSimNetwork *nocAddr);
  void Step();

  void RegisterCallbacksInterface(booksim::TransactionCompleteCB *readDone, booksim::TransactionCompleteCB *writeDone, BookSimNetwork *nocAddr);
  void CallbackEverything(int pid, BookSimNetwork *nocAddr);
  
  void Init();
  void UpdateStats();
  void DisplayStats();
  void DisplayOverallStats();
  
  simTime GetIcntTime() const;
  int getNocFrequency(){return nocFrequencyMHz;}
  int getPacketSize(){ return packetSize;}
  int getHopDelay(){ return hopDelay;}
  uint64_t getNocCurCycle(){return nocCurCycle;}
  void setNocCurCycle(uint64_t cycle){nocCurCycle = cycle;}
  int getCntStepCalls(){return cntStepCalls;}
  int getNodes();

protected:
  uint64_t nocCurCycle;
  class _BoundaryBufferItem {
    public:
      _BoundaryBufferItem():_packet_n(0) {}
      inline unsigned Size(void) const { return _buffer.size(); }
      inline bool HasPacket() const { return _packet_n; }
      void* PopPacket();
      void* TopPacket() const;
      void PushFlitData(void* data,bool is_tail);
			int getIniBool(const std::string &field, bool *val);
    private:
      queue<void *> _buffer;
      queue<bool> _tail_flag;
      int _packet_n;
    };
  typedef queue<Flit*> _EjectionBufferItem;
  
  void _CreateBuffer( );
  
  // size: [subnets][nodes][vcs]
  vector<vector<vector<_BoundaryBufferItem> > > _boundary_buffer;
  unsigned int _boundary_buffer_capacity;
  // size: [subnets][nodes][vcs]
  vector<vector<vector<_EjectionBufferItem> > > _ejection_buffer;
  // size:[subnets][nodes]
  vector<vector<queue<Flit* > > > _ejected_flit_queue;
  
  unsigned int _ejection_buffer_capacity;
  unsigned int _input_buffer_capacity;
  
  vector<vector<int> > _round_robin_turn; //keep track of _boundary_buffer last used in icnt_pop
  

  TrafficManager* _traffic_manager;
  unsigned _flit_size;
  IntersimConfig* _icnt_config;
  vector<Network *> _net;
  int _vcs;
  int _subnets;
  int nocFrequencyMHz;
  int packetSize;
  int hopDelay;
  uint32_t zsimPhaseLength;
  int iN; //dimension

private:
  int stepsBeforeUpdateStats, stepsCnt;
  int cntStepCalls = 0;
  int outStandingPackets = 0;
  int skippedSteps = 0;
  int nonSkippedSteps = 0;
  std::map<BookSimNetwork*,Callback_t*> ReturnReadData;
  Callback_t* WriteDataDone;

  uint32_t totalInFlightPackets;
  deque<pair<int, pair<int, pair<int, pair< int, pair<int,int>>>>>>  zllPackets;  // zll, cycle of insertion,  srcx, srcy, dstx, dsty
};

#endif


