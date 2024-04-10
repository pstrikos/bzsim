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
  curCycle = 0;

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

  _vcs = _icnt_config->GetInt("num_vcs");

  _CreateBuffer();

  stepsBeforeUpdateStats = _traffic_manager->GetStepsBeforeUpdateStats();
  stepsCnt = 0;

}

void InterconnectInterface::Init()
{
  _traffic_manager->Init();
}

void InterconnectInterface::ManuallyGeneratePacket(int source, int dest, int size, int ctime, uint64_t addr){
  _traffic_manager->_ManuallyGeneratePacket(source,  dest,  size,  ctime, addr);
}

void InterconnectInterface::DisplayStats()
{
  _traffic_manager->UpdateStats();
//   _traffic_manager->DisplayStats();
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
  _traffic_manager->_Step();
  if (++stepsCnt == stepsBeforeUpdateStats){
    DisplayStats();
    stepsCnt = 0;
  }
}

void InterconnectInterface::RegisterCallbacksInterface(Callback_t *readDone, Callback_t *writeDone){
  // _traffic_manager->RegisterCallbacks(readDone, writeDone); 
  ReturnReadData.push_back(readDone);
	WriteDataDone = writeDone;
}

void InterconnectInterface::CallbackEverything(uint64_t addr){
  for (auto& callback : ReturnReadData) {
    callback->operator()(0, addr, 1);
  }
}