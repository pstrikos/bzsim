// // Copyright (c) 2009-2013, Tor M. Aamodt, Dongdong Li, Ali Bakhoda
// // The University of British Columbia
// // All rights reserved.
// //
// // Redistribution and use in source and binary forms, with or without
// // modification, are permitted provided that the following conditions are met:
// //
// // Redistributions of source code must retain the above copyright notice, this
// // list of conditions and the following disclaimer.
// // Redistributions in binary form must reproduce the above copyright notice, this
// // list of conditions and the following disclaimer in the documentation and/or
// // other materials provided with the distribution.
// // Neither the name of The University of British Columbia nor the names of its
// // contributors may be used to endorse or promote products derived from this
// // software without specific prior written permission.
// //
// // THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
// // ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// // WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// // DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
// // FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
// // DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// // SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
// // CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
// // OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// // OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include <fstream>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <utility>
#include <algorithm>

#include "interconnect_interface.hpp"
#include "routefunc.hpp"
#include "globals.hpp"
#include "trafficmanager.hpp"
#include "power_module.hpp"
#include "flit.hpp"
#include "booksim.hpp"
#include "intersim_config.hpp"
#include "network.hpp"
#include <sys/time.h>

InterconnectInterface* InterconnectInterface::New(const char* const config_file)
{
  if (! config_file ) {
    cout << "Interconnect Requires a configfile" << endl;
    exit (-1);
  }
  InterconnectInterface* icnt_interface = new InterconnectInterface();
  icnt_interface->_icnt_config = new IntersimConfig();

  icnt_interface->_icnt_config->ParseFile(config_file);

  return icnt_interface;
}

InterconnectInterface::InterconnectInterface()
{
}

InterconnectInterface::~InterconnectInterface()
{
  delete _traffic_manager;
  _traffic_manager = NULL;
  delete _icnt_config;
}

void InterconnectInterface::CreateInterconnect()
{
  nocCurCycle = 0;

  InitializeRoutingMap(*_icnt_config);

  gPrintActivity = (_icnt_config->GetInt("print_activity") > 0);
  gTrace = (_icnt_config->GetInt("viewer_trace") > 0);

  string watch_out_file = _icnt_config->GetStr( "watch_out" );
  if(watch_out_file == "") {
    gWatchOut = NULL;
  } else if(watch_out_file == "-") {
    gWatchOut = &cout;
  } else {
    gWatchOut = new ofstream(watch_out_file.c_str());
  }

  _subnets = _icnt_config->GetInt("subnets");
  assert(_subnets);

  /*To include a new network, must register the network here
   *add an else if statement with the name of the network
   */
  _net.resize(_subnets);
  for (int i = 0; i < _subnets; ++i) {
    ostringstream name;
    name << "network_" << i;
    _net[i] = Network::New( *_icnt_config, name.str() );
  }

  // assert(_icnt_config->GetStr("sim_type") == "gpgpusim");
  _traffic_manager = TrafficManager::New( *_icnt_config, _net, this) ;
  iN = _traffic_manager->getNodes();
  dim = sqrt(iN);
  _flit_size = _icnt_config->GetInt( "flit_size" );

  // Config for interface buffers
  if (_icnt_config->GetInt("ejection_buffer_size")) {
    _ejection_buffer_capacity = _icnt_config->GetInt( "ejection_buffer_size" ) ;
  } else {
    _ejection_buffer_capacity = _icnt_config->GetInt( "vc_buf_size" );
  }

  _boundary_buffer_capacity = _icnt_config->GetInt( "boundary_buffer_size" ) ;
  assert(_boundary_buffer_capacity);
  if (_icnt_config->GetInt("input_buffer_size")) {
    _input_buffer_capacity = _icnt_config->GetInt("input_buffer_size");
  } else {
    _input_buffer_capacity = 9;
  }

  nocFrequencyMHz = _icnt_config->GetInt("noc_frequency_mhz");
  packetSize      = _icnt_config->GetInt("packet_size");

  int routing_delay  = _icnt_config->GetInt("routing_delay");
  int crossbar_delay = _icnt_config->GetInt("st_prepare_delay") + _icnt_config->GetInt("st_final_delay");
  const int link_delay = 1;
  int vc_alloc_delay = _icnt_config->GetInt("vc_alloc_delay");
  int sw_alloc_delay = _icnt_config->GetInt("sw_alloc_delay");
  hopDelay =  routing_delay + crossbar_delay + link_delay + 
          + (_icnt_config->GetInt("speculative") ? max(vc_alloc_delay, sw_alloc_delay) : (vc_alloc_delay + sw_alloc_delay));

  _vcs = _icnt_config->GetInt("num_vcs");

  _CreateBuffer();

  stepsBeforeUpdateStats = _traffic_manager->GetStepsBeforeUpdateStats();
  stepsCnt = 0;

  totalInFlightPackets = 0;

}

void InterconnectInterface::Init()
{
  _traffic_manager->Init();
}

int InterconnectInterface::ManuallyGeneratePacket(int source, int dest, int size, int ctime, uint64_t addr, BookSimNetwork *nocAddr){
    outStandingPackets++;
    int packId = _traffic_manager->_ManuallyGeneratePacket(source,  dest,  size,  ctime, addr, nocAddr);
    return packId;
  }

void InterconnectInterface::UpdateStats()
{
  _traffic_manager->UpdateStats();
}

void InterconnectInterface::DisplayStats()
{
  _traffic_manager->DisplayStats();
}

void InterconnectInterface::DisplayOverallStats()
{
  // hack: booksim2 use _drain_time and calculate delta time based on it, but we don't, change this if you have a better idea
  _traffic_manager->updateDrainTime();
  // hack: also _total_sims equals to number of kernel calls

  _traffic_manager->_UpdateOverallStats();
  _traffic_manager->DisplayOverallStats();

  double skippedPerc = (100.0*skippedSteps)/(skippedSteps+nonSkippedSteps);
        std::cout << "Number of non-skipped steps = " << nonSkippedSteps << std::endl
                  << "Number of skipped steps = " << skippedSteps 
                      << " ( " <<  std::round(skippedPerc * 100)/100 <<  " \% )" << std::endl
                  << "Total steps = " << skippedSteps + nonSkippedSteps << std::endl;
}


int InterconnectInterface::GetIcntTime() const
{
  return _traffic_manager->_time;
}

void InterconnectInterface::_CreateBuffer()
{
  unsigned nodes = _net[0]->NumNodes();

  _boundary_buffer.resize(_subnets);
  _ejection_buffer.resize(_subnets);
  _round_robin_turn.resize(_subnets);
  _ejected_flit_queue.resize(_subnets);

  for (int subnet = 0; subnet < _subnets; ++subnet) {
    _ejection_buffer[subnet].resize(nodes);
    _boundary_buffer[subnet].resize(nodes);
    _round_robin_turn[subnet].resize(nodes);
    _ejected_flit_queue[subnet].resize(nodes);

    for (unsigned node=0;node < nodes;++node){
      _ejection_buffer[subnet][node].resize(_vcs);
      _boundary_buffer[subnet][node].resize(_vcs);
    }
  }
}

int cntprints = 0;

void InterconnectInterface::Step(){
#ifndef _NO_OPT_
  if(outStandingPackets == 0){
    skippedSteps++;
    _traffic_manager->incrTime(); // TODO: do I really need that or is it OK if the noc has a different clock?
    return;
  }
#endif
  nonSkippedSteps++;
  cntStepCalls++;
  _traffic_manager->_Step();

#if !defined(_SKIP_STEP_) && !defined(_EMPTY_STEP_)
  if (++stepsCnt >= stepsBeforeUpdateStats){
    UpdateStats();
    // DisplayStats();
    stepsCnt = 0;
  }
#endif
}

void InterconnectInterface::RegisterCallbacksInterface(Callback_t *readDone, Callback_t *writeDone, BookSimNetwork *nocAddr){
  ReturnReadData.insert(std::make_pair(nocAddr, readDone));
  WriteDataDone = writeDone;
}


void InterconnectInterface::CallbackEverything(int pid, BookSimNetwork *nocAddr){
  outStandingPackets--;
  for (auto& iter: ReturnReadData) {
    if(iter.first == nocAddr){
      iter.second->operator()(0, pid, 1);
      return;
    }
  }

  std::cout << "no callback sent" << std::endl;
}

int InterconnectInterface::getNodes(){ return iN;}