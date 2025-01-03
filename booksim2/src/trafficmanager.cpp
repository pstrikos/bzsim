// $Id$

/*
  Copyright (c) 2007-2015, Trustees of The Leland Stanford Junior University
  All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are met:

  Redistributions of source code must retain the above copyright notice, this 
  list of conditions and the following disclaimer.
  Redistributions in binary form must reproduce the above copyright notice, this
  list of conditions and the following disclaimer in the documentation and/or
  other materials provided with the distribution.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
  ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
  WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE 
  DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
  ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
  (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
  ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <sstream>
#include <cmath>
#include <fstream>
#include <limits>
#include <cstdlib>
#include <ctime>

#include "booksim.hpp"
#include "booksim_config.hpp"
#include "trafficmanager.hpp"
#include "batchtrafficmanager.hpp"
#include "random_utils.hpp" 
#include "vc.hpp"
#include "packet_reply_info.hpp"
// #define TRACK_INJECTION
// #define CALC_INJECTION_RATE
#ifdef CALC_INJECTION_RATE
    vector <int> cnt_flits(16,0) ;
    int cnt_flit_total = 0;
    vector <int> cnt_gen_flits(16,0) ;
    int cnt_gen_flit_total = 0;
    int cnt_prev_gen_flit_total = 0;
    vector <int> cnt_ret_flits(16,0) ;
    int cnt_ret_flit_total = 0;
    vector <int> cnt_msr_flits(16,0) ;
    int cnt_msr_flit_total = 0;
    int cnt_prev_msr_flit_total = 0;
#endif

    int cnt = 0;
TrafficManager * TrafficManager::New(Configuration const & config,
                                     vector<Network *> const & net,InterconnectInterface* parentInterface)
{
    TrafficManager * result = NULL;
    string sim_type = config.GetStr("sim_type");
    if((sim_type == "latency") || (sim_type == "throughput")) {
        result = new TrafficManager(config, net, parentInterface);
    } else if(sim_type == "batch") {
        result = new BatchTrafficManager(config, net, parentInterface);
    } else {
        cerr << "Unknown simulation type: " << sim_type << endl;
    } 
    return result;
}

TrafficManager::TrafficManager( const Configuration &config, const vector<Network *> & net, InterconnectInterface* parentInterface )
    : Module( 0, "traffic_manager" ), _net(net), _empty_network(false), _deadlock_timer(0), _reset_time(0), _drain_time(-1), _cur_id(0), _cur_pid(0), _time(0)
{
    parent = parentInterface;
    _nodes = _net[0]->NumNodes( );
    _routers = _net[0]->NumRouters( );

    _vcs = config.GetInt("num_vcs");
    nocFrequencyMHz = config.GetInt("noc_frequency_mhz");
    _subnets = config.GetInt("subnets");
 
    _subnet.resize(Flit::NUM_FLIT_TYPES);
    _subnet[Flit::READ_REQUEST] = config.GetInt("read_request_subnet");
    _subnet[Flit::READ_REPLY] = config.GetInt("read_reply_subnet");
    _subnet[Flit::WRITE_REQUEST] = config.GetInt("write_request_subnet");
    _subnet[Flit::WRITE_REPLY] = config.GetInt("write_reply_subnet");

    // ============ Message priorities ============ 

    string priority = config.GetStr( "priority" );

    if ( priority == "class" ) {
        _pri_type = class_based;
    } else if ( priority == "age" ) {
        _pri_type = age_based;
    } else if ( priority == "network_age" ) {
        _pri_type = network_age_based;
    } else if ( priority == "local_age" ) {
        _pri_type = local_age_based;
    } else if ( priority == "queue_length" ) {
        _pri_type = queue_length_based;
    } else if ( priority == "hop_count" ) {
        _pri_type = hop_count_based;
    } else if ( priority == "sequence" ) {
        _pri_type = sequence_based;
    } else if ( priority == "none" ) {
        _pri_type = none;
    } else {
        Error( "Unkown priority value: " + priority );
    }

    // ============ Routing ============ 

    string rf = config.GetStr("routing_function") + "_" + config.GetStr("topology");
    // searches for dor_mesh in gRoutingMap map and returns an iterator pointing there
    // gRoutingFunctionMap["dor_mesh"]            = &dim_order_mesh;
    map<string, tRoutingFunction>::const_iterator rf_iter = gRoutingFunctionMap.find(rf); //find() finds the element rf and returns an iterator pointing to it
    if(rf_iter == gRoutingFunctionMap.end()) { // .end() returns the past-the-end element of the map
        Error("Invalid routing function: " + rf);
    }
    _rf = rf_iter->second; // _rf now containts the &dim_order_mesh
  
    _lookahead_routing = !config.GetInt("routing_delay");
    _noq = config.GetInt("noq");
    if(_noq) {
        if(!_lookahead_routing) {
            Error("NOQ requires lookahead routing to be enabled.");
        }
    }

    // ============ Traffic ============ 

    _classes = config.GetInt("classes");

    _use_read_write = config.GetIntArray("use_read_write");
    if(_use_read_write.empty()) {
        _use_read_write.push_back(config.GetInt("use_read_write"));
    }
    _use_read_write.resize(_classes, _use_read_write.back());

    _write_fraction = config.GetFloatArray("write_fraction");
    if(_write_fraction.empty()) {
        _write_fraction.push_back(config.GetFloat("write_fraction"));
    }
    _write_fraction.resize(_classes, _write_fraction.back());

    _read_request_size = config.GetIntArray("read_request_size");
    if(_read_request_size.empty()) {
        _read_request_size.push_back(config.GetInt("read_request_size"));
    }
    _read_request_size.resize(_classes, _read_request_size.back());

    _read_reply_size = config.GetIntArray("read_reply_size");
    if(_read_reply_size.empty()) {
        _read_reply_size.push_back(config.GetInt("read_reply_size"));
    }
    _read_reply_size.resize(_classes, _read_reply_size.back());

    _write_request_size = config.GetIntArray("write_request_size");
    if(_write_request_size.empty()) {
        _write_request_size.push_back(config.GetInt("write_request_size"));
    }
    _write_request_size.resize(_classes, _write_request_size.back());

    _write_reply_size = config.GetIntArray("write_reply_size");
    if(_write_reply_size.empty()) {
        _write_reply_size.push_back(config.GetInt("write_reply_size"));
    }
    _write_reply_size.resize(_classes, _write_reply_size.back());

    string packet_size_str = config.GetStr("packet_size");
    if(packet_size_str.empty()) {
        _packet_size.push_back(vector<int>(1, config.GetInt("packet_size")));
    } else {
        vector<string> packet_size_strings = tokenize_str(packet_size_str);
        for(size_t i = 0; i < packet_size_strings.size(); ++i) {
            _packet_size.push_back(tokenize_int(packet_size_strings[i]));
        }
    }
    _packet_size.resize(_classes, _packet_size.back());

    string packet_size_rate_str = config.GetStr("packet_size_rate");
    if(packet_size_rate_str.empty()) {
        int rate = config.GetInt("packet_size_rate");
        assert(rate >= 0);
        for(int c = 0; c < _classes; ++c) {
            int size = _packet_size[c].size();
            _packet_size_rate.push_back(vector<int>(size, rate));
            _packet_size_max_val.push_back(size * rate - 1);
        }
    } else {
        vector<string> packet_size_rate_strings = tokenize_str(packet_size_rate_str);
        packet_size_rate_strings.resize(_classes, packet_size_rate_strings.back());
        for(int c = 0; c < _classes; ++c) {
            vector<int> rates = tokenize_int(packet_size_rate_strings[c]);
            rates.resize(_packet_size[c].size(), rates.back());
            _packet_size_rate.push_back(rates);
            int size = rates.size();
            int max_val = -1;
            for(int i = 0; i < size; ++i) {
                int rate = rates[i];
                assert(rate >= 0);
                max_val += rate;
            }
            _packet_size_max_val.push_back(max_val);
        }
    }
  
    for(int c = 0; c < _classes; ++c) {
        if(_use_read_write[c]) {
            _packet_size[c] = 
                vector<int>(1, (_read_request_size[c] + _read_reply_size[c] +
                                _write_request_size[c] + _write_reply_size[c]) / 2);
            _packet_size_rate[c] = vector<int>(1, 1);
            _packet_size_max_val[c] = 0;
        }
    }

    _load = config.GetFloatArray("injection_rate"); 
    if(_load.empty()) {
        _load.push_back(config.GetFloat("injection_rate"));
    }
    _load.resize(_classes, _load.back());

    if(config.GetInt("injection_rate_uses_flits")) {
        for(int c = 0; c < _classes; ++c)
            _load[c] /= _GetAveragePacketSize(c);
    }

    _traffic = config.GetStrArray("traffic");
    _traffic.resize(_classes, _traffic.back());

    _traffic_pattern.resize(_classes);

    _class_priority = config.GetIntArray("class_priority"); 
    if(_class_priority.empty()) {
        _class_priority.push_back(config.GetInt("class_priority"));
    }
    _class_priority.resize(_classes, _class_priority.back());

    vector<string> injection_process = config.GetStrArray("injection_process");
    injection_process.resize(_classes, injection_process.back());

    _injection_process.resize(_classes);

    for(int c = 0; c < _classes; ++c) {
        _traffic_pattern[c] = TrafficPattern::New(_traffic[c], _nodes, &config);
        _injection_process[c] = InjectionProcess::New(injection_process[c], _nodes, _load[c], &config);
    }

    // ============ Injection VC states  ============ 

    _buf_states.resize(_nodes);
    _last_vc.resize(_nodes);
    _last_class.resize(_nodes);

    for ( int source = 0; source < _nodes; ++source ) {
        _buf_states[source].resize(_subnets);
        _last_class[source].resize(_subnets, 0);
        _last_vc[source].resize(_subnets);
        for ( int subnet = 0; subnet < _subnets; ++subnet ) {
            ostringstream tmp_name;
            tmp_name << "terminal_buf_state_" << source << "_" << subnet;
            BufferState * bs = new BufferState( config, this, tmp_name.str( ) ); // create bs based on config file (VCs, buffer/VC etc)
            int vc_alloc_delay = config.GetInt("vc_alloc_delay");
            int sw_alloc_delay = config.GetInt("sw_alloc_delay");
            int router_latency = config.GetInt("routing_delay") + (config.GetInt("speculative") ? max(vc_alloc_delay, sw_alloc_delay) : (vc_alloc_delay + sw_alloc_delay));
            int min_latency = 1 + _net[subnet]->GetInject(source)->GetLatency() + router_latency + _net[subnet]->GetInjectCred(source)->GetLatency();
            bs->SetMinLatency(min_latency); 
            _buf_states[source][subnet] = bs; // set one injection buffer per node with the configuration specified before
            _last_vc[source][subnet].resize(_classes, -1);
        }
    }

#ifdef TRACK_FLOWS
    _outstanding_credits.resize(_classes);
    for(int c = 0; c < _classes; ++c) {
        _outstanding_credits[c].resize(_subnets, vector<int>(_nodes, 0));
    }
    _outstanding_classes.resize(_nodes);
    for(int n = 0; n < _nodes; ++n) {
        _outstanding_classes[n].resize(_subnets, vector<queue<int> >(_vcs));
    }
#endif

    // ============ Injection queues ============ 

    _qtime.resize(_nodes);
    _qdrained.resize(_nodes);
    _partial_packets.resize(_nodes);

    for ( int s = 0; s < _nodes; ++s ) {
        _qtime[s].resize(_classes);
        _qdrained[s].resize(_classes);
        _partial_packets[s].resize(_classes);
    }

    _total_in_flight_flits.resize(_classes);
    _measured_in_flight_flits.resize(_classes);
    _retired_packets.resize(_classes);

    _packet_seq_no.resize(_nodes);
    _repliesPending.resize(_nodes);
    _requestsOutstanding.resize(_nodes);

    _hold_switch_for_packet = config.GetInt("hold_switch_for_packet");

    // ============ Simulation parameters ============ 

    _total_sims = config.GetInt( "sim_count" );

    _router.resize(_subnets);
    for (int i=0; i < _subnets; ++i) {
        _router[i] = _net[i]->GetRouters();
    }

    //seed the network
    int seed;
    if(config.GetStr("seed") == "time") {
      seed = int(time(NULL));
      cout << "SEED: seed=" << seed << endl;
    } else {
      seed = config.GetInt("seed");
    }
    RandomSeed(seed);

    _measure_latency = (config.GetStr("sim_type") == "latency");

    _sample_period = config.GetInt( "sample_period" );
    _max_samples    = config.GetInt( "max_samples" );
    _warmup_periods = config.GetInt( "warmup_periods" );

    _measure_stats = config.GetIntArray( "measure_stats" );
    if(_measure_stats.empty()) {
        _measure_stats.push_back(config.GetInt("measure_stats"));
    }
    _measure_stats.resize(_classes, _measure_stats.back());
    _pair_stats = (config.GetInt("pair_stats")==1);

    _latency_thres = config.GetFloatArray( "latency_thres" );
    if(_latency_thres.empty()) {
        _latency_thres.push_back(config.GetFloat("latency_thres"));
    }
    _latency_thres.resize(_classes, _latency_thres.back());

    _warmup_threshold = config.GetFloatArray( "warmup_thres" );
    if(_warmup_threshold.empty()) {
        _warmup_threshold.push_back(config.GetFloat("warmup_thres"));
    }
    _warmup_threshold.resize(_classes, _warmup_threshold.back());

    _acc_warmup_threshold = config.GetFloatArray( "acc_warmup_thres" );
    if(_acc_warmup_threshold.empty()) {
        _acc_warmup_threshold.push_back(config.GetFloat("acc_warmup_thres"));
    }
    _acc_warmup_threshold.resize(_classes, _acc_warmup_threshold.back());

    _stopping_threshold = config.GetFloatArray( "stopping_thres" );
    if(_stopping_threshold.empty()) {
        _stopping_threshold.push_back(config.GetFloat("stopping_thres"));
    }
    _stopping_threshold.resize(_classes, _stopping_threshold.back());

    _acc_stopping_threshold = config.GetFloatArray( "acc_stopping_thres" );
    if(_acc_stopping_threshold.empty()) {
        _acc_stopping_threshold.push_back(config.GetFloat("acc_stopping_thres"));
    }
    _acc_stopping_threshold.resize(_classes, _acc_stopping_threshold.back());

    _include_queuing = config.GetInt( "include_queuing" );

    _print_csv_results = config.GetInt( "print_csv_results" );
    _deadlock_warn_timeout = config.GetInt( "deadlock_warn_timeout" );

    string watch_file = config.GetStr( "watch_file" );
    if((watch_file != "") && (watch_file != "-")) {
        _LoadWatchList(watch_file);
    }

    vector<int> watch_flits = config.GetIntArray("watch_flits");
    for(size_t i = 0; i < watch_flits.size(); ++i) {
        _flits_to_watch.insert(watch_flits[i]);
    }
  
    vector<int> watch_packets = config.GetIntArray("watch_packets");
    for(size_t i = 0; i < watch_packets.size(); ++i) {
        _packets_to_watch.insert(watch_packets[i]);
    }

    string stats_out_file = config.GetStr( "stats_out" );
    if(stats_out_file == "") {
        _stats_out = NULL;
    } else if(stats_out_file == "-") {
        _stats_out = &cout;
    } else {
        _stats_out = new ofstream(stats_out_file.c_str());
        config.WriteMatlabFile(_stats_out);
    }
#ifdef TRACK_FLOWS
    _injected_flits.resize(_classes, vector<int>(_nodes, 0));
    _ejected_flits.resize(_classes, vector<int>(_nodes, 0));
    string injected_flits_out_file = config.GetStr( "injected_flits_out" );
    if(injected_flits_out_file == "") {
        _injected_flits_out = NULL;
    } else {
        _injected_flits_out = new ofstream(injected_flits_out_file.c_str());
    }
    string received_flits_out_file = config.GetStr( "received_flits_out" );
    if(received_flits_out_file == "") {
        _received_flits_out = NULL;
    } else {
        _received_flits_out = new ofstream(received_flits_out_file.c_str());
    }
    string stored_flits_out_file = config.GetStr( "stored_flits_out" );
    if(stored_flits_out_file == "") {
        _stored_flits_out = NULL;
    } else {
        _stored_flits_out = new ofstream(stored_flits_out_file.c_str());
    }
    string sent_flits_out_file = config.GetStr( "sent_flits_out" );
    if(sent_flits_out_file == "") {
        _sent_flits_out = NULL;
    } else {
        _sent_flits_out = new ofstream(sent_flits_out_file.c_str());
    }
    string outstanding_credits_out_file = config.GetStr( "outstanding_credits_out" );
    if(outstanding_credits_out_file == "") {
        _outstanding_credits_out = NULL;
    } else {
        _outstanding_credits_out = new ofstream(outstanding_credits_out_file.c_str());
    }
    string ejected_flits_out_file = config.GetStr( "ejected_flits_out" );
    if(ejected_flits_out_file == "") {
        _ejected_flits_out = NULL;
    } else {
        _ejected_flits_out = new ofstream(ejected_flits_out_file.c_str());
    }
    string active_packets_out_file = config.GetStr( "active_packets_out" );
    if(active_packets_out_file == "") {
        _active_packets_out = NULL;
    } else {
        _active_packets_out = new ofstream(active_packets_out_file.c_str());
    }
#endif

#ifdef TRACK_CREDITS
    string used_credits_out_file = config.GetStr( "used_credits_out" );
    if(used_credits_out_file == "") {
        _used_credits_out = NULL;
    } else {
        _used_credits_out = new ofstream(used_credits_out_file.c_str());
    }
    string free_credits_out_file = config.GetStr( "free_credits_out" );
    if(free_credits_out_file == "") {
        _free_credits_out = NULL;
    } else {
        _free_credits_out = new ofstream(free_credits_out_file.c_str());
    }
    string max_credits_out_file = config.GetStr( "max_credits_out" );
    if(max_credits_out_file == "") {
        _max_credits_out = NULL;
    } else {
        _max_credits_out = new ofstream(max_credits_out_file.c_str());
    }
#endif

    // ============ Statistics ============ 

    stepsBeforeUpdateStats = config.GetInt( "step_cnt_update" );


    _plat_stats.resize(_classes);
    _overall_min_plat.resize(_classes, 0.0);
    _overall_avg_plat.resize(_classes, 0.0);
    _overall_max_plat.resize(_classes, 0.0);

    _nlat_stats.resize(_classes);
    _overall_min_nlat.resize(_classes, 0.0);
    _overall_avg_nlat.resize(_classes, 0.0);
    _overall_max_nlat.resize(_classes, 0.0);

    _flat_stats.resize(_classes);
    _overall_min_flat.resize(_classes, 0.0);
    _overall_avg_flat.resize(_classes, 0.0);
    _overall_max_flat.resize(_classes, 0.0);

    _frag_stats.resize(_classes);
    _overall_min_frag.resize(_classes, 0.0);
    _overall_avg_frag.resize(_classes, 0.0);
    _overall_max_frag.resize(_classes, 0.0);

    if(_pair_stats){
        _pair_plat.resize(_classes);
        _pair_nlat.resize(_classes);
        _pair_flat.resize(_classes);
    }
  
    _hop_stats.resize(_classes);
    _overall_hop_stats.resize(_classes, 0.0);
  
    _sent_packets.resize(_classes);
    _overall_min_sent_packets.resize(_classes, 0.0);
    _overall_avg_sent_packets.resize(_classes, 0.0);
    _overall_max_sent_packets.resize(_classes, 0.0);
    _accepted_packets.resize(_classes);
    _injected_packets.resize(_classes);
    _ejected_packets.resize(_classes);
    _overall_min_accepted_packets.resize(_classes, 0.0);
    _overall_avg_accepted_packets.resize(_classes, 0.0);
    _overall_max_accepted_packets.resize(_classes, 0.0);

    _sent_flits.resize(_classes);
    _overall_min_sent.resize(_classes, 0.0);
    _overall_avg_sent.resize(_classes, 0.0);
    _overall_max_sent.resize(_classes, 0.0);
    _accepted_flits.resize(_classes);
    _overall_min_accepted.resize(_classes, 0.0);
    _overall_avg_accepted.resize(_classes, 0.0);
    _overall_max_accepted.resize(_classes, 0.0);

#ifdef EXTRA_STATS
    _createdPackets.resize(_classes);
    _createdFlits.resize(_classes);
    _createdPacketsLLC.resize(_classes);
    _createdPacketsRest.resize(_classes);
    _latency_introduced.resize(_classes);
#endif

#ifdef TRACK_STALLS
    _buffer_busy_stalls.resize(_classes);
    _buffer_conflict_stalls.resize(_classes);
    _buffer_full_stalls.resize(_classes);
    _buffer_reserved_stalls.resize(_classes);
    _crossbar_conflict_stalls.resize(_classes);
    _overall_buffer_busy_stalls.resize(_classes, 0);
    _overall_buffer_conflict_stalls.resize(_classes, 0);
    _overall_buffer_full_stalls.resize(_classes, 0);
    _overall_buffer_reserved_stalls.resize(_classes, 0);
    _overall_crossbar_conflict_stalls.resize(_classes, 0);
#endif

    for ( int c = 0; c < _classes; ++c ) {
        ostringstream tmp_name;

        tmp_name << "plat_stat_" << c;
        _plat_stats[c] = new Stats( this, tmp_name.str( ), 1.0, 1000 );
        _stats[tmp_name.str()] = _plat_stats[c];
        tmp_name.str("");

        tmp_name << "nlat_stat_" << c;
        _nlat_stats[c] = new Stats( this, tmp_name.str( ), 1.0, 1000 );
        _stats[tmp_name.str()] = _nlat_stats[c];
        tmp_name.str("");

        tmp_name << "flat_stat_" << c;
        _flat_stats[c] = new Stats( this, tmp_name.str( ), 1.0, 1000 );
        _stats[tmp_name.str()] = _flat_stats[c];
        tmp_name.str("");

        tmp_name << "frag_stat_" << c;
        _frag_stats[c] = new Stats( this, tmp_name.str( ), 1.0, 100 );
        _stats[tmp_name.str()] = _frag_stats[c];
        tmp_name.str("");

        tmp_name << "hop_stat_" << c;
        _hop_stats[c] = new Stats( this, tmp_name.str( ), 1.0, 20 );
        _stats[tmp_name.str()] = _hop_stats[c];
        tmp_name.str("");

        if(_pair_stats){
            _pair_plat[c].resize(_nodes*_nodes);
            _pair_nlat[c].resize(_nodes*_nodes);
            _pair_flat[c].resize(_nodes*_nodes);
        }

        _sent_packets[c].resize(_nodes, 0);
        _accepted_packets[c].resize(_nodes, 0);
        _ejected_packets[c].resize(_nodes,0);
        _injected_packets[c].resize(_nodes, 0);
        _sent_flits[c].resize(_nodes, 0);
        _accepted_flits[c].resize(_nodes, 0);
#ifdef EXTRA_STATS
        _createdPackets.resize(gX*gY);
        _createdFlits.resize(gX*gY);
        _createdPacketsLLC.resize(gX*gY);
        _createdPacketsRest.resize(gX*gY);
        _latency_introduced[c].resize(gX*gY,0);
        for(int n = 0; n < gX*gY; n++){
            _createdPackets[n].resize(gX*gY,0);
            _createdPacketsLLC[n].resize(gX*gY,0);
            _createdPacketsRest[n].resize(gX*gY,0);
        }
#endif

#ifdef TRACK_STALLS
        _buffer_busy_stalls[c].resize(_subnets*_routers, 0);
        _buffer_conflict_stalls[c].resize(_subnets*_routers, 0);
        _buffer_full_stalls[c].resize(_subnets*_routers, 0);
        _buffer_reserved_stalls[c].resize(_subnets*_routers, 0);
        _crossbar_conflict_stalls[c].resize(_subnets*_routers, 0);
#endif
        if(_pair_stats){
            for ( int i = 0; i < _nodes; ++i ) {
                for ( int j = 0; j < _nodes; ++j ) {
                    tmp_name << "pair_plat_stat_" << c << "_" << i << "_" << j;
                    _pair_plat[c][i*_nodes+j] = new Stats( this, tmp_name.str( ), 1.0, 250 );
                    _stats[tmp_name.str()] = _pair_plat[c][i*_nodes+j];
                    tmp_name.str("");
	  
                    tmp_name << "pair_nlat_stat_" << c << "_" << i << "_" << j;
                    _pair_nlat[c][i*_nodes+j] = new Stats( this, tmp_name.str( ), 1.0, 250 );
                    _stats[tmp_name.str()] = _pair_nlat[c][i*_nodes+j];
                    tmp_name.str("");
	  
                    tmp_name << "pair_flat_stat_" << c << "_" << i << "_" << j;
                    _pair_flat[c][i*_nodes+j] = new Stats( this, tmp_name.str( ), 1.0, 250 );
                    _stats[tmp_name.str()] = _pair_flat[c][i*_nodes+j];
                    tmp_name.str("");
                }
            }
        }
    }

    _slowest_flit.resize(_classes, -1);
    _slowest_packet.resize(_classes, -1);

    outstandingFlits.resize(_subnets);
    for(int s = 0 ; s < _subnets; ++s){
        outstandingFlits[s].resize(_nodes);
        _net[s]->setOutstandingFlits(&outstandingFlits[s]); // set the pointer in each network/router/channel to the outStandingFlits array used in trafficmanager
    }

}

TrafficManager::~TrafficManager( )
{

    for ( int source = 0; source < _nodes; ++source ) {
        for ( int subnet = 0; subnet < _subnets; ++subnet ) {
            delete _buf_states[source][subnet];
        }
    }
  
    for ( int c = 0; c < _classes; ++c ) {
        delete _plat_stats[c];
        delete _nlat_stats[c];
        delete _flat_stats[c];
        delete _frag_stats[c];
        delete _hop_stats[c];

        delete _traffic_pattern[c];
        delete _injection_process[c];
        if(_pair_stats){
            for ( int i = 0; i < _nodes; ++i ) {
                for ( int j = 0; j < _nodes; ++j ) {
                    delete _pair_plat[c][i*_nodes+j];
                    delete _pair_nlat[c][i*_nodes+j];
                    delete _pair_flat[c][i*_nodes+j];
                }
            }
        }
    }
  
    if(gWatchOut && (gWatchOut != &cout)) delete gWatchOut;
    if(_stats_out && (_stats_out != &cout)) delete _stats_out;
    // if(_overall_stats_out && (_overall_stats_out != &cout)) delete _overall_stats_out;

#ifdef TRACK_FLOWS
    if(_injected_flits_out) delete _injected_flits_out;
    if(_received_flits_out) delete _received_flits_out;
    if(_stored_flits_out) delete _stored_flits_out;
    if(_sent_flits_out) delete _sent_flits_out;
    if(_outstanding_credits_out) delete _outstanding_credits_out;
    if(_ejected_flits_out) delete _ejected_flits_out;
    if(_active_packets_out) delete _active_packets_out;
#endif

#ifdef TRACK_CREDITS
    if(_used_credits_out) delete _used_credits_out;
    if(_free_credits_out) delete _free_credits_out;
    if(_max_credits_out) delete _max_credits_out;
#endif

    PacketReplyInfo::FreeAll();
    Flit::FreeAll();
    Credit::FreeAll();
}

void TrafficManager::Init()
{
    _time = 0;
    _requestsOutstanding.assign(_nodes, 0);
    for (int i=0;i<_nodes;i++) {
        while(!_repliesPending[i].empty()) {
            _repliesPending[i].front()->Free();
            _repliesPending[i].pop_front();
        }
    }

    // reset queuetime for all sources
    for ( int s = 0; s < _nodes; ++s ) {
        _qtime[s].assign(_classes, 0);
        _qdrained[s].assign(_classes, false);
    }

    _sim_state    = running;

    _ClearStats( );

    for ( int s = 0; s < _nodes; ++s ) {
        _qtime[s].assign(_classes, 0);
        _qdrained[s].assign(_classes, false);
    }

    for(int c = 0; c < _classes; ++c) {
        _traffic_pattern[c]->reset();
        _injection_process[c]->reset();
    }

}


void TrafficManager::_RetireFlit( Flit *f, int dest )
{
    #ifdef CALC_INJECTION_RATE
        cnt_ret_flits[dest]++;
        cnt_ret_flit_total++;
    #endif
    _deadlock_timer = 0;

    assert(_total_in_flight_flits[f->cl].count(f->id) > 0);
    _total_in_flight_flits[f->cl].erase(f->id);
  
    if(f->record) {
        assert(_measured_in_flight_flits[f->cl].count(f->id) > 0);
        _measured_in_flight_flits[f->cl].erase(f->id);
    }

    if ( f->watch ) { 
        *gWatchOut << GetSimTime() << " | "
                   << "node" << dest << " | "
                   << "Retiring flit " << f->id 
                   << " (packet " << f->pid
                   << ", src = " << f->src 
                   << ", dest = " << f->dest
                   << ", hops = " << f->hops
                   << ", flat = " << f->atime - f->itime
                   << ")." << endl;
    }

#ifdef EXTRA_STATS
    _latency_introduced[f->cl][f->src] +=  f->atime - f->itime;
#endif
    if ( f->head && ( f->dest != dest ) ) {
        ostringstream err;
        err << "Flit " << f->id << " arrived at incorrect output " << dest;
        Error( err.str( ) );
    }
  
    if((_slowest_flit[f->cl] < 0) ||
       (_flat_stats[f->cl]->Max() < (f->atime - f->itime)))
        _slowest_flit[f->cl] = f->id;

    _flat_stats[f->cl]->AddSample( (double) (f->atime - f->itime));
   
    if(_pair_stats){
        _pair_flat[f->cl][f->src*_nodes+dest]->AddSample( (double) (f->atime - f->itime) );
    }
      
    if ( f->tail ) {
        Flit * head;
        if(f->head) {
            head = f;
        } else {
            map<int, Flit *>::iterator iter = _retired_packets[f->cl].find(f->pid);
            assert(iter != _retired_packets[f->cl].end());
            head = iter->second;
            _retired_packets[f->cl].erase(iter);
            assert(head->head);
            assert(f->pid == head->pid);
        }
        if ( f->watch ) { 
            *gWatchOut << GetSimTime() << " | "
                       << "node" << dest << " | "
                       << "Retiring packet " << f->pid 
                       << " (plat = " << f->atime - head->ctime
                       << ", nlat = " << f->atime - head->itime
                       << ", frag = " << (f->atime - head->atime) - (f->id - head->id) // NB: In the spirit of solving problems using ugly hacks, we compute the packet length by taking advantage of the fact that the IDs of flits within a packet are contiguous.
                       << ", src = " << head->src 
                       << ", dest = " << head->dest
                       << ")." << endl;
        }

        //code the source of request, look carefully, its tricky ;)
        if (f->type == Flit::READ_REQUEST || f->type == Flit::WRITE_REQUEST) {
            PacketReplyInfo* rinfo = PacketReplyInfo::New();
            rinfo->source = f->src;
            rinfo->time = f->atime;
            rinfo->record = f->record;
            rinfo->type = f->type;
            // TODO: be very carefull to what is happening here!!!!
            _repliesPending[dest].push_back(rinfo);
        } else {
            if(f->type == Flit::READ_REPLY || f->type == Flit::WRITE_REPLY  ){
                _requestsOutstanding[dest]--;
            } else if(f->type == Flit::ANY_TYPE) {
                _requestsOutstanding[f->src]--;
            }
      
        }

        // Only record statistics once per packet (at tail)
        if ( ( _sim_state == warming_up ) || f->record ) {
      
            _hop_stats[f->cl]->AddSample( f->hops );

            if((_slowest_packet[f->cl] < 0) ||
               (_plat_stats[f->cl]->Max() < (f->atime - head->itime)))
                _slowest_packet[f->cl] = f->pid;
            _plat_stats[f->cl]->AddSample( (double) (f->atime - head->ctime));
            _nlat_stats[f->cl]->AddSample( (double) (f->atime - head->itime));
            _frag_stats[f->cl]->AddSample( (double) ((f->atime - head->atime) - (f->id - head->id) ));
   
            if(_pair_stats){
                _pair_plat[f->cl][f->src*_nodes+dest]->AddSample( (double) (f->atime - head->ctime) );
                _pair_nlat[f->cl][f->src*_nodes+dest]->AddSample( (double) (f->atime - head->itime) );
            }
        }
    
        if(f != head) {
            head->Free();
        }
    
    }
  
    if(f->head && !f->tail) {
        _retired_packets[f->cl].insert(make_pair(f->pid, f));
    } else {
        f->Free();
    }
}

void TrafficManager::_Step( )
{   
    cntStepCalls++;

#if defined(_SKIP_STEP_) || defined(_EMPTY_STEP_)
    for(auto & itPack : _in_flight_packets){
        if(--itPack.second == 0){ // its time to eject
            _in_flight_packets.erase(itPack.first);

            auto it = _in_flight_req_address.find(itPack.first);
            parent->CallbackEverything(it->second.second, it->second.first);
            _in_flight_req_address.erase(it);
        }
    }
#endif


// leave this on if _EMPTY_STEP_ but not if _SKIP_STEP_
#ifndef _SKIP_STEP_
    bool flits_in_flight = false;
    for(int c = 0; c < _classes; ++c) {
        flits_in_flight |= !_total_in_flight_flits[c].empty(); // check that there is at least one flit waiting in a class
    }
    if(flits_in_flight && (_deadlock_timer++ >= _deadlock_warn_timeout)){
        _deadlock_timer = 0;
        cout << "WARNING: Possible network deadlock.\n";
    }

    // flits[] stores pairs of <node, ejected flits> for all nodes in a subnet. 
    // Initialize vector "flits" of size "_subnets"
    vector<map<int, Flit *> > flits(_subnets); 


    // --------------------------  Step 1----------------------------------------- //
    // Read from the channel's and routers outputs. 
    // This includes ejecting from the network by reading the _outut of the ejection channels, 
    // and inserting flits/credits that have traversed channels into routers, by reading 
    // each channel's _output
    for ( int subnet = 0; subnet < _subnets; ++subnet ) {
        // for every node in the subnet, check if there are ejected flits waiting
        // and if there are insert them into flits[]
        for ( int n = 0; n < _nodes; ++n ) {
            if(outstandingFlits[subnet][n] != 0 ){
            
            // ReadFlit returns _output from the _eject channel of node n
                Flit * const f = _net[subnet]->ReadFlit( n ); // returns _eject[n]._output
                // If router n has a flit in its _eject queue output
                // store it in the flits[] vector along all other flits.
                
                if ( f ) { 
                    if(f->watch) {  
                        *gWatchOut << GetSimTime() << " | "
                                << "node" << n << " | "
                                << "Ejecting flit " << f->id
                                << " (packet " << f->pid << ")"
                                << " from VC " << f->vc
                                << "." << endl;
                    }
                    flits[subnet].insert(make_pair(n, f)); // add the outgoing ejected flit into flits[]
                    if((_sim_state == warming_up) || (_sim_state == running)) { 
                        ++_accepted_flits[f->cl][n];
                        if(f->tail) {
                            ++_accepted_packets[f->cl][n];
                            ++_ejected_packets[f->cl][f->src];
                        }
                    }

                    outstandingFlits[subnet][n] -= 1; // node n just injected one flit

                }
            }
            
            // If there is a credit, it means that the router received a flit
            Credit * const c = _net[subnet]->ReadCredit( n ); // read the credit given by the local RNI
            if ( c ) { 
#ifdef TRACK_FLOWS
                for(set<int>::const_iterator iter = c->vc.begin(); iter != c->vc.end(); ++iter) {
                    int const vc = *iter;
                    assert(!_outstanding_classes[n][subnet][vc].empty());
                    int cl = _outstanding_classes[n][subnet][vc].front();
                    _outstanding_classes[n][subnet][vc].pop();
                    assert(_outstanding_credits[cl][subnet][n] > 0);
                    --_outstanding_credits[cl][subnet][n];
                }
#endif
                _buf_states[n][subnet]->ProcessCredit(c);
                c->Free();
            }
        }
        /* Read the inputs for everything in _time_modules which includes every channel in the network, 
          including inject, eject, and credit channels. 
          Each channels ReadInput function will be pushing flits from its _input to its _waiting_queue
          to simulate the delay needed for channel traversal
          Each router's ReadInput will be pushing incoming flits/credits to the input buffers and 
          setting the _active flag for each router that has no incoming flits/credits */
        _net[subnet]->ReadInputs( );
    }

#ifdef TRACK_INJECTION
    printInjectedPackets();
    cnt++;
#endif


    // Step 2 : VC selection of injected flits and injection from input node to the network

    // for each node, get from the _partial_packet the flit that is waiting to be injected, 
    // find a proper VC for it, pop it out of the vector and write it to the injection queue of the router 
    for(int subnet = 0; subnet < _subnets; ++subnet) {

        for(int n = 0; n < _nodes; ++n) {
            assert(outstandingFlits[subnet][n] >= 0);

            if(outstandingFlits[subnet][n] != 0 ){
            
                Flit * f = NULL;

                // on destination buffer for each node 
                BufferState * const dest_buf = _buf_states[n][subnet]; 

                int const last_class = _last_class[n][subnet];

                int class_limit = _classes;

                if(_hold_switch_for_packet) { // 0
                    list<Flit *> const & pp = _partial_packets[n][last_class];
                    if(!pp.empty() && !pp.front()->head && 
                    !dest_buf->IsFullFor(pp.front()->vc)) {
                        f = pp.front();
                        assert(f->vc == _last_vc[n][subnet][last_class]); 
                        // if we're holding the connection, we don't need to check that class 
                        // again in the for loop
                        --class_limit;
                    }
                }

                for(int i = 1; i <= class_limit; ++i) { // 1 class right now

                    int const c = (last_class + i) % _classes;

                    // pp contains the part of _partial_packet that belong to a specific class of the node
                    list<Flit *> const & pp = _partial_packets[n][c];
                
                    if(pp.empty()) {
                        continue;
                    }
                    
                    Flit * const cf = pp.front(); // get the first flit in the queue (oldest)
                    assert(cf);
                    assert(cf->cl == c);
        
                    if(cf->subnetwork != subnet) {
                        continue;
                    }

                    if(f && (f->pri >= cf->pri)) {
                        continue;
                    }

                    // I'm going to be creating packets ahead of time
                    // so if they are in the queue back but it is not yet the time 
                    // to use them, they should be skipped
                    if (cf->ctime > _time){
                        continue;
                    }


                    // Step 2.1: Virtual Channel Selection
                    // -- remember that when creating the flit, the VC is set to -1
                    if(cf->head && cf->vc == -1) { // Find first available VC
        
                        OutputSet route_set;
                        _rf(NULL, cf, -1, &route_set, true); //
                        set<OutputSet::sSetElement> const & os = route_set.GetSet();
                        assert(os.size() == 1);
                        OutputSet::sSetElement const & se = *os.begin();
                        assert(se.output_port == -1);
                        int vc_start = se.vc_start;
                        int vc_end = se.vc_end;
                        int vc_count = vc_end - vc_start + 1;
                        if(_noq) { //next-hop-output queueing
                            assert(_lookahead_routing);
                            const FlitChannel * inject = _net[subnet]->GetInject(n); //return _inject[n]
                            const Router * router = inject->GetSink(); //return _routerSink
                            assert(router);
                            int in_channel = inject->GetSinkPort();

                            // NOTE: Because the lookahead is not for injection, but for the 
                            // first hop, we have to temporarily set cf's VC to be non-negative 
                            // in order to avoid seting of an assertion in the routing function.
                            cf->vc = vc_start;
                            _rf(router, cf, in_channel, &cf->la_route_set, false); // Lookahead route info:  OutputSet la_route_set;
                            cf->vc = -1;

                            if(cf->watch) {
                                *gWatchOut << GetSimTime() << " | "
                                        << "node" << n << " | "
                                        << "Generating lookahead routing info for flit " << cf->id
                                        << " (NOQ)." << endl;
                            }
                            set<OutputSet::sSetElement> const sl = cf->la_route_set.GetSet();
                            assert(sl.size() == 1);
                            int next_output = sl.begin()->output_port;
                            vc_count /= router->NumOutputs();
                            vc_start += next_output * vc_count;
                            vc_end = vc_start + vc_count - 1;
                            assert(vc_start >= se.vc_start && vc_start <= se.vc_end);
                            assert(vc_end >= se.vc_start && vc_end <= se.vc_end);
                            assert(vc_start <= vc_end);
                        }
                        if(cf->watch) {
                            *gWatchOut << GetSimTime() << " | " << FullName() << " | "
                                    << "Finding output VC for flit " << cf->id
                                    << ":" << endl;
                        }
                        for(int i = 1; i <= vc_count; ++i) {
                            int const lvc = _last_vc[n][subnet][c];
                            int const vc =
                                (lvc < vc_start || lvc > vc_end) ?
                                vc_start :
                                (vc_start + (lvc - vc_start + i) % vc_count);
                            assert((vc >= vc_start) && (vc <= vc_end));
                            if(!dest_buf->IsAvailableFor(vc)) { // return _in_use_by[vc] < 0
                                if(cf->watch) {
                                    *gWatchOut << GetSimTime() << " | " << FullName() << " | "
                                            << "  Output VC " << vc << " is busy." << endl;
                                }
                            } else {
                                if(dest_buf->IsFullFor(vc)) {
                                    if(cf->watch) {
                                        *gWatchOut << GetSimTime() << " | " << FullName() << " | "
                                                << "  Output VC " << vc << " is full." << endl;
                                    }
                                } else {
                                    if(cf->watch) {
                                        *gWatchOut << GetSimTime() << " | " << FullName() << " | "
                                                << "  Selected output VC " << vc << "." << endl;
                                    }
                                    cf->vc = vc; // now set the vc for that head flit
                                    break;
                                }
                            }
                        }
                        
                    }
                    
                    
                    if(cf->vc == -1) { // if vc is still -1, it means I wasn't able to find anything
                        if(cf->watch) {
                            *gWatchOut << GetSimTime() << " | " << FullName() << " | "
                                    << "No output VC found for flit " << cf->id
                                    << "." << endl;
                        }
                    } else {
                        if(dest_buf->IsFullFor(cf->vc)) { //return _in_use_by[vc] < 0
                            if(cf->watch) {
                                *gWatchOut << GetSimTime() << " | " << FullName() << " | "
                                        << "Selected output VC " << cf->vc
                                        << " is full for flit " << cf->id
                                        << "." << endl;
                            }
                        } else {
                            f = cf;
                        }
                    }
                }

                // if I managed to find a suitable VC for the selcted flit cf, and that buffer is available 
                // then f holds that cf that just passed through Virtual Channel Selection

                // Step 2.2: Flit injection in the network and writting the flit in the injected router's input

                if(f) {

                    assert(f->subnetwork == subnet);

                    int const c = f->cl;

                    if(f->head) {
        
                        if (_lookahead_routing) {
                            if(!_noq) {
                                const FlitChannel * inject = _net[subnet]->GetInject(n); // get the _inject channel of the current node
                                const Router * router = inject->GetSink(); // get the destination router of the _inject channel
                                assert(router);
                                int in_channel = inject->GetSinkPort();
                                //( const Router *, const Flit *, int in_channel, OutputSet *, bool );
                                // sig faults here
                                _rf(router, f, in_channel/*the output of inject*/, &f->la_route_set, false); // sets lookahead (la_route_set)
                                if(f->watch) {
                                    *gWatchOut << GetSimTime() << " | "
                                            << "node" << n << " | "
                                            << "Generating lookahead routing info for flit " << f->id
                                            << "." << endl;
                                }
                            } else if(f->watch) {
                                *gWatchOut << GetSimTime() << " | "
                                        << "node" << n << " | "
                                        << "Already generated lookahead routing info for flit " << f->id
                                        << " (NOQ)." << endl;
                            }
                        } else {
                            f->la_route_set.Clear();
                        }

                        dest_buf->TakeBuffer(f->vc);
                        _last_vc[n][subnet][c] = f->vc;
                    }
        
                    _last_class[n][subnet] = c;

                    // by now I have found the VC that the flit will use, so pop it
                    // Note that the flit is still in the _total_in_flight_flits
                    // cout << "------------------------------------------" <<
                            // "curr time " << _time << ": flit " << f->id << " created at time " << f->ctime << endl;
                    _partial_packets[n][c].pop_front(); 

    #ifdef TRACK_FLOWS
                    ++_outstanding_credits[c][subnet][n];
                    _outstanding_classes[n][subnet][f->vc].push(c);
    #endif

                    // update the downstream buffer with the information of the flit that will soon be traversing
                    // the channel to reach it
                    dest_buf->SendingFlit(f);
        
                    if(_pri_type == network_age_based) {
                        f->pri = numeric_limits<int>::max() - _time;
                        assert(f->pri >= 0);
                    }
        
                    if(f->watch) {
                        *gWatchOut << GetSimTime() << " | "
                                << "node" << n << " | "
                                << "Injecting flit " << f->id
                                << " into subnet " << subnet
                                << " at time " << _time
                                << " with priority " << f->pri
                                << "." << endl;
                    }
                    f->itime = _time;

                    #ifdef CALC_INJECTION_RATE
                        cnt_flits[n]++;
                        cnt_flit_total++;
                    #endif
                    // Pass selected VC "back" to the next flit in the queue
                    if(!_partial_packets[n][c].empty() && !f->tail) {
                        Flit * const nf = _partial_packets[n][c].front();
                        nf->vc = f->vc;
                    }
        
                    if((_sim_state == warming_up) || (_sim_state == running)) {
                        ++_sent_flits[c][n];
                        if(f->head) {
                            ++_sent_packets[c][n];
                        }
                    }
        
    #ifdef TRACK_FLOWS
                    ++_injected_flits[c][n];
    #endif
        


                    // allocation is finished so WRITE the flit to the router's input channel's _input
                    // sets _inject[n]._input = f
                    _net[subnet]->WriteFlit(f, n);  
	
                }
            }

        } 
    }
    



    for(int subnet = 0; subnet < _subnets; ++subnet) {
        for(int n = 0; n < _nodes; ++n) {
            // iterator find (const key_type& k) searches the container for an 
            //element with a key equivalent to k and returns an iterator to it
            // if found.
            // So, here we look on whether router n has ejected a flit
            map<int, Flit *>::const_iterator iter = flits[subnet].find(n); // flits[] holds _eject[n]._output for each router
            if(iter != flits[subnet].end()) {
                Flit * const f = iter->second;
                f->atime = _time;
                if(f->watch) {
                    *gWatchOut << GetSimTime() << " | "
                               << "node" << n << " | "
                               << "Injecting credit for VC " << f->vc 
                               << " into subnet " << subnet 
                               << "." << endl;
                }
                // if there is a flit waiting to be ejected, I need to create a new credit --
                // and distribute it to the upstream router
                Credit * const c = Credit::New();
                c->vc.insert(f->vc);
                _net[subnet]->WriteCredit(c, n);
	
#ifdef TRACK_FLOWS
                ++_ejected_flits[f->cl][n];
#endif
	
                _RetireFlit(f, n); // here the flit is also deleted from the total_in_flight_flits
                if (f->tail == true){
                        auto it = _in_flight_req_address.find(f->pid);
                        // _in_flight_req_address.insert(make_pair(pid,make_pair(nocAddr,addr) ));
                        parent->CallbackEverything(f->pid, it->second.first);
                        _in_flight_req_address.erase(it);  
                }

            }
        }
        flits[subnet].clear(); // remove all flits that were ejected

        
        _net[subnet]->Evaluate( );
        _net[subnet]->WriteOutputs( ); //  sets _output for each flitChannel from its FIFO's output
    }

#endif
    ++_time;
    assert(_time);
    if(gTrace){
        // cout<<"TIME "<<_time<<endl;
    }
}


bool TrafficManager::_PacketsOutstanding( ) const
{
    for ( int c = 0; c < _classes; ++c ) {
        if ( _measure_stats[c] ) {
            if ( _measured_in_flight_flits[c].empty() ) {
	
                // for ( int s = 0; s < _nodes; ++s ) {
                    // if ( !_qdrained[s][c] ) {
#ifdef DEBUG_DRAIN
                        cout << "waiting on queue " << s << " class " << c;
                        cout << ", time = " << _time << " qtime = " << _qtime[s][c] << endl;
#endif
                        // return true;
                    // }
                // }
            } else {
#ifdef DEBUG_DRAIN
                cout << "in flight = " << _measured_in_flight_flits[c].size() << endl;
#endif
                return true;
            }
        }
    }
    return false;
}

void TrafficManager::_ClearStats( )
{
    _slowest_flit.assign(_classes, -1);
    _slowest_packet.assign(_classes, -1);

    for ( int c = 0; c < _classes; ++c ) {

        _plat_stats[c]->Clear( );
        _nlat_stats[c]->Clear( );
        _flat_stats[c]->Clear( );

        _frag_stats[c]->Clear( );

        _sent_packets[c].assign(_nodes, 0);
        _accepted_packets[c].assign(_nodes, 0);
        _sent_flits[c].assign(_nodes, 0);
        _accepted_flits[c].assign(_nodes, 0);

#ifdef TRACK_STALLS
        _buffer_busy_stalls[c].assign(_subnets*_routers, 0);
        _buffer_conflict_stalls[c].assign(_subnets*_routers, 0);
        _buffer_full_stalls[c].assign(_subnets*_routers, 0);
        _buffer_reserved_stalls[c].assign(_subnets*_routers, 0);
        _crossbar_conflict_stalls[c].assign(_subnets*_routers, 0);
#endif
        if(_pair_stats){
            for ( int i = 0; i < _nodes; ++i ) {
                for ( int j = 0; j < _nodes; ++j ) {
                    _pair_plat[c][i*_nodes+j]->Clear( );
                    _pair_nlat[c][i*_nodes+j]->Clear( );
                    _pair_flat[c][i*_nodes+j]->Clear( );
                }
            }
        }
        _hop_stats[c]->Clear();

    }

    _reset_time = _time;
}

void TrafficManager::_ComputeStats( const vector<int> & stats, int *sum, int *min, int *max, int *min_pos, int *max_pos ) const 
{
    int const count = stats.size();
    assert(count > 0);

    if(min_pos) {
        *min_pos = 0;
    }
    if(max_pos) {
        *max_pos = 0;
    }

    if(min) {
        *min = stats[0];
    }
    if(max) {
        *max = stats[0];
    }

    *sum = stats[0];

    for ( int i = 1; i < count; ++i ) {
        int curr = stats[i];
        if ( min  && ( curr < *min ) ) {
            *min = curr;
            if ( min_pos ) {
                *min_pos = i;
            }
        }
        if ( max && ( curr > *max ) ) {
            *max = curr;
            if ( max_pos ) {
                *max_pos = i;
            }
        }
        *sum += curr;
    }
}

void TrafficManager::_DisplayRemaining( ostream & os ) const 
{
    for(int c = 0; c < _classes; ++c) {

        map<int, Flit *>::const_iterator iter;
        int i;

        os << "Class " << c << ":" << endl;

        os << "Remaining flits: ";
        for ( iter = _total_in_flight_flits[c].begin( ), i = 0;
              ( iter != _total_in_flight_flits[c].end( ) ) && ( i < 10 );
              iter++, i++ ) {
            os << iter->first << " ";
        }
        if(_total_in_flight_flits[c].size() > 10)
            os << "[...] ";
    
        os << "(" << _total_in_flight_flits[c].size() << " flits)" << endl;
    
        os << "Measured flits: ";
        for ( iter = _measured_in_flight_flits[c].begin( ), i = 0;
              ( iter != _measured_in_flight_flits[c].end( ) ) && ( i < 10 );
              iter++, i++ ) {
            os << iter->first << " ";
        }
        if(_measured_in_flight_flits[c].size() > 10)
            os << "[...] ";
    
        os << "(" << _measured_in_flight_flits[c].size() << " flits)" << endl;
    
    }
}


void TrafficManager::_UpdateOverallStats() {
    for ( int c = 0; c < _classes; ++c ) {
    
        if(_measure_stats[c] == 0) {
            continue;
        }
    
        _overall_min_plat[c] += _plat_stats[c]->Min();
        _overall_avg_plat[c] += _plat_stats[c]->Average();
        _overall_max_plat[c] += _plat_stats[c]->Max();
        _overall_min_nlat[c] += _nlat_stats[c]->Min();
        _overall_avg_nlat[c] += _nlat_stats[c]->Average();
        _overall_max_nlat[c] += _nlat_stats[c]->Max();
        _overall_min_flat[c] += _flat_stats[c]->Min();
        _overall_avg_flat[c] += _flat_stats[c]->Average();
        _overall_max_flat[c] += _flat_stats[c]->Max();
    
        _overall_min_frag[c] += _frag_stats[c]->Min();
        _overall_avg_frag[c] += _frag_stats[c]->Average();
        _overall_max_frag[c] += _frag_stats[c]->Max();

        _overall_hop_stats[c] += _hop_stats[c]->Average();

        int count_min, count_sum, count_max;
        double rate_min, rate_sum, rate_max;
        double rate_avg;
        double time_delta = (double)(_drain_time - _reset_time);
        _ComputeStats( _sent_flits[c], &count_sum, &count_min, &count_max );
        rate_min = (double)count_min / time_delta;
        rate_sum = (double)count_sum / time_delta;
        rate_max = (double)count_max / time_delta;
        rate_avg = rate_sum / (double) (gX * gY); //(double)_nodes;
        _overall_min_sent[c] += rate_min;
        _overall_avg_sent[c] += rate_avg;
        _overall_max_sent[c] += rate_max;
        _ComputeStats( _sent_packets[c], &count_sum, &count_min, &count_max );
        rate_min = (double)count_min / time_delta;
        rate_sum = (double)count_sum / time_delta;
        rate_max = (double)count_max / time_delta;
        rate_avg = rate_sum /  (double) (gX * gY); //(double)_nodes;
        _overall_min_sent_packets[c] += rate_min;
        _overall_avg_sent_packets[c] += rate_avg;
        _overall_max_sent_packets[c] += rate_max;
        _ComputeStats( _accepted_flits[c], &count_sum, &count_min, &count_max );
        rate_min = (double)count_min / time_delta;
        rate_sum = (double)count_sum / time_delta;
        rate_max = (double)count_max / time_delta;
        rate_avg = rate_sum /  (double) (gX * gY); //(double)_nodes;
        _overall_min_accepted[c] += rate_min;
        _overall_avg_accepted[c] += rate_avg;
        _overall_max_accepted[c] += rate_max;
        _ComputeStats( _accepted_packets[c], &count_sum, &count_min, &count_max );
        rate_min = (double)count_min / time_delta;
        rate_sum = (double)count_sum / time_delta;
        rate_max = (double)count_max / time_delta;
        rate_avg = rate_sum /  (double) (gX * gY); //(double)_nodes;
        _overall_min_accepted_packets[c] += rate_min;
        _overall_avg_accepted_packets[c] += rate_avg;
        _overall_max_accepted_packets[c] += rate_max;

#ifdef TRACK_STALLS
        _ComputeStats(_buffer_busy_stalls[c], &count_sum);
        rate_sum = (double)count_sum / time_delta;
        rate_avg = rate_sum / (double)(_subnets*_routers);
        _overall_buffer_busy_stalls[c] += rate_avg;
        _ComputeStats(_buffer_conflict_stalls[c], &count_sum);
        rate_sum = (double)count_sum / time_delta;
        rate_avg = rate_sum / (double)(_subnets*_routers);
        _overall_buffer_conflict_stalls[c] += rate_avg;
        _ComputeStats(_buffer_full_stalls[c], &count_sum);
        rate_sum = (double)count_sum / time_delta;
        rate_avg = rate_sum / (double)(_subnets*_routers);
        _overall_buffer_full_stalls[c] += rate_avg;
        _ComputeStats(_buffer_reserved_stalls[c], &count_sum);
        rate_sum = (double)count_sum / time_delta;
        rate_avg = rate_sum / (double)(_subnets*_routers);
        _overall_buffer_reserved_stalls[c] += rate_avg;
        _ComputeStats(_crossbar_conflict_stalls[c], &count_sum);
        rate_sum = (double)count_sum / time_delta;
        rate_avg = rate_sum / (double)(_subnets*_routers);
        _overall_crossbar_conflict_stalls[c] += rate_avg;
#endif

    }
}

void TrafficManager::WriteStats(ostream & os) const {
  
    os << "%=================================" << endl;

    for(int c = 0; c < _classes; ++c) {
    
        if(_measure_stats[c] == 0) {
            continue;
        }
    
        //c+1 due to matlab array starting at 1
        os << "plat(" << c+1 << ") = " << _plat_stats[c]->Average() << ";" << endl
           << "plat_hist(" << c+1 << ",:) = " << *_plat_stats[c] << ";" << endl
           << "nlat(" << c+1 << ") = " << _nlat_stats[c]->Average() << ";" << endl
           << "nlat_hist(" << c+1 << ",:) = " << *_nlat_stats[c] << ";" << endl
           << "flat(" << c+1 << ") = " << _flat_stats[c]->Average() << ";" << endl
           << "flat_hist(" << c+1 << ",:) = " << *_flat_stats[c] << ";" << endl
           << "frag_hist(" << c+1 << ",:) = " << *_frag_stats[c] << ";" << endl
           << "hops(" << c+1 << ",:) = " << *_hop_stats[c] << ";" << endl;
        if(_pair_stats){
            os<< "pair_sent(" << c+1 << ",:) = [ ";
            for(int i = 0; i < _nodes; ++i) {
                for(int j = 0; j < _nodes; ++j) {
                    os << _pair_plat[c][i*_nodes+j]->NumSamples() << " ";
                }
            }
            os << "];" << endl
               << "pair_plat(" << c+1 << ",:) = [ ";
            for(int i = 0; i < _nodes; ++i) {
                for(int j = 0; j < _nodes; ++j) {
                    os << _pair_plat[c][i*_nodes+j]->Average( ) << " ";
                }
            }
            os << "];" << endl
               << "pair_nlat(" << c+1 << ",:) = [ ";
            for(int i = 0; i < _nodes; ++i) {
                for(int j = 0; j < _nodes; ++j) {
                    os << _pair_nlat[c][i*_nodes+j]->Average( ) << " ";
                }
            }
            os << "];" << endl
               << "pair_flat(" << c+1 << ",:) = [ ";
            for(int i = 0; i < _nodes; ++i) {
                for(int j = 0; j < _nodes; ++j) {
                    os << _pair_flat[c][i*_nodes+j]->Average( ) << " ";
                }
            }
        }

        double time_delta = (double)(_drain_time - _reset_time);

        os << "];" << endl
           << "sent_packets(" << c+1 << ",:) = [ ";
        for ( int d = 0; d < _nodes; ++d ) {
            os << (double)_sent_packets[c][d] / time_delta << " ";
        }
        os << "];" << endl
           << "accepted_packets(" << c+1 << ",:) = [ ";
        for ( int d = 0; d < _nodes; ++d ) {
            os << (double)_accepted_packets[c][d] / time_delta << " ";
        }
        os << "];" << endl
           << "sent_flits(" << c+1 << ",:) = [ ";
        for ( int d = 0; d < _nodes; ++d ) {
            os << (double)_sent_flits[c][d] / time_delta << " ";
        }
        os << "];" << endl
           << "accepted_flits(" << c+1 << ",:) = [ ";
        for ( int d = 0; d < _nodes; ++d ) {
            os << (double)_accepted_flits[c][d] / time_delta << " ";
        }
        os << "];" << endl
           << "sent_packet_size(" << c+1 << ",:) = [ ";
        for ( int d = 0; d < _nodes; ++d ) {
            os << (double)_sent_flits[c][d] / (double)_sent_packets[c][d] << " ";
        }
        os << "];" << endl
           << "accepted_packet_size(" << c+1 << ",:) = [ ";
        for ( int d = 0; d < _nodes; ++d ) {
            os << (double)_accepted_flits[c][d] / (double)_accepted_packets[c][d] << " ";
        }
        os << "];" << endl;
#ifdef TRACK_STALLS
        os << "buffer_busy_stalls(" << c+1 << ",:) = [ ";
        for ( int d = 0; d < _subnets*_routers; ++d ) {
            os << (double)_buffer_busy_stalls[c][d] / time_delta << " ";
        }
        os << "];" << endl
           << "buffer_conflict_stalls(" << c+1 << ",:) = [ ";
        for ( int d = 0; d < _subnets*_routers; ++d ) {
            os << (double)_buffer_conflict_stalls[c][d] / time_delta << " ";
        }
        os << "];" << endl
           << "buffer_full_stalls(" << c+1 << ",:) = [ ";
        for ( int d = 0; d < _subnets*_routers; ++d ) {
            os << (double)_buffer_full_stalls[c][d] / time_delta << " ";
        }
        os << "];" << endl
           << "buffer_reserved_stalls(" << c+1 << ",:) = [ ";
        for ( int d = 0; d < _subnets*_routers; ++d ) {
            os << (double)_buffer_reserved_stalls[c][d] / time_delta << " ";
        }
        os << "];" << endl
           << "crossbar_conflict_stalls(" << c+1 << ",:) = [ ";
        for ( int d = 0; d < _subnets*_routers; ++d ) {
            os << (double)_crossbar_conflict_stalls[c][d] / time_delta << " ";
        }
        os << "];" << endl;
#endif
    }
}

void TrafficManager::UpdateStats() {
#if defined(TRACK_FLOWS) || defined(TRACK_STALLS)
    for(int c = 0; c < _classes; ++c) {
#ifdef TRACK_FLOWS
        {
            char trail_char = (c == _classes - 1) ? '\n' : ',';
            if(_injected_flits_out) *_injected_flits_out << _injected_flits[c] << trail_char;
            _injected_flits[c].assign(_nodes, 0);
            if(_ejected_flits_out) *_ejected_flits_out << _ejected_flits[c] << trail_char;
            _ejected_flits[c].assign(_nodes, 0);
        }
#endif
        for(int subnet = 0; subnet < _subnets; ++subnet) {
#ifdef TRACK_FLOWS
            if(_outstanding_credits_out) *_outstanding_credits_out << _outstanding_credits[c][subnet] << ',';
            if(_stored_flits_out) *_stored_flits_out << vector<int>(_nodes, 0) << ',';
#endif
            for(int router = 0; router < _routers; ++router) {
                Router * const r = _router[subnet][router];
#ifdef TRACK_FLOWS
                char trail_char = 
                    ((router == _routers - 1) && (subnet == _subnets - 1) && (c == _classes - 1)) ? '\n' : ',';
                if(_received_flits_out) *_received_flits_out << r->GetReceivedFlits(c) << trail_char;
                if(_stored_flits_out) *_stored_flits_out << r->GetStoredFlits(c) << trail_char;
                if(_sent_flits_out) *_sent_flits_out << r->GetSentFlits(c) << trail_char;
                if(_outstanding_credits_out) *_outstanding_credits_out << r->GetOutstandingCredits(c) << trail_char;
                if(_active_packets_out) *_active_packets_out << r->GetActivePackets(c) << trail_char;
                r->ResetFlowStats(c);
#endif
#ifdef TRACK_STALLS
                _buffer_busy_stalls[c][subnet*_routers+router] += r->GetBufferBusyStalls(c);
                _buffer_conflict_stalls[c][subnet*_routers+router] += r->GetBufferConflictStalls(c);
                _buffer_full_stalls[c][subnet*_routers+router] += r->GetBufferFullStalls(c);
                _buffer_reserved_stalls[c][subnet*_routers+router] += r->GetBufferReservedStalls(c);
                _crossbar_conflict_stalls[c][subnet*_routers+router] += r->GetCrossbarConflictStalls(c);
                r->ResetStallStats(c);
#endif
            }
        }
    }
#ifdef TRACK_FLOWS
    if(_injected_flits_out) *_injected_flits_out << flush;
    if(_received_flits_out) *_received_flits_out << flush;
    if(_stored_flits_out) *_stored_flits_out << flush;
    if(_sent_flits_out) *_sent_flits_out << flush;
    if(_outstanding_credits_out) *_outstanding_credits_out << flush;
    if(_ejected_flits_out) *_ejected_flits_out << flush;
    if(_active_packets_out) *_active_packets_out << flush;
#endif
#endif

#ifdef TRACK_CREDITS
    for(int s = 0; s < _subnets; ++s) {
        for(int n = 0; n < _nodes; ++n) {
            BufferState const * const bs = _buf_states[n][s];
            for(int v = 0; v < _vcs; ++v) {
                if(_used_credits_out) *_used_credits_out << bs->OccupancyFor(v) << ',';
                if(_free_credits_out) *_free_credits_out << bs->AvailableFor(v) << ',';
                if(_max_credits_out) *_max_credits_out << bs->LimitFor(v) << ',';
            }
        }
        for(int r = 0; r < _routers; ++r) {
            Router const * const rtr = _router[s][r];
            char trail_char = 
                ((r == _routers - 1) && (s == _subnets - 1)) ? '\n' : ',';
            if(_used_credits_out) *_used_credits_out << rtr->UsedCredits() << trail_char;
            if(_free_credits_out) *_free_credits_out << rtr->FreeCredits() << trail_char;
            if(_max_credits_out) *_max_credits_out << rtr->MaxCredits() << trail_char;
        }
    }
    if(_used_credits_out) *_used_credits_out << flush;
    if(_free_credits_out) *_free_credits_out << flush;
    if(_max_credits_out) *_max_credits_out << flush;
#endif

}

void TrafficManager::DisplayStats(ostream & os) const {
  
    for(int c = 0; c < _classes; ++c) {
    
        if(_measure_stats[c] == 0) {
            continue;
        }
    
        cout << "Class " << c << ":" << endl;
    
        cout 
            << "Packet latency average = " << _plat_stats[c]->Average() << endl
            << "\tminimum = " << _plat_stats[c]->Min() << endl
            << "\tmaximum = " << _plat_stats[c]->Max() << endl
            << "Network latency average = " << _nlat_stats[c]->Average() << endl
            << "\tminimum = " << _nlat_stats[c]->Min() << endl
            << "\tmaximum = " << _nlat_stats[c]->Max() << endl
            << "Slowest packet = " << _slowest_packet[c] << endl
            << "Flit latency average = " << _flat_stats[c]->Average() << endl
            << "\tminimum = " << _flat_stats[c]->Min() << endl
            << "\tmaximum = " << _flat_stats[c]->Max() << endl
            << "Slowest flit = " << _slowest_flit[c] << endl
            << "Fragmentation average = " << _frag_stats[c]->Average() << endl
            << "\tminimum = " << _frag_stats[c]->Min() << endl
            << "\tmaximum = " << _frag_stats[c]->Max() << endl;
    
        int count_sum, count_min, count_max;
        double rate_sum, rate_min, rate_max;
        double rate_avg;
        int sent_packets, sent_flits, accepted_packets, accepted_flits;
        int min_pos, max_pos;
        double time_delta = (double)(_time - _reset_time);
        _ComputeStats(_sent_packets[c], &count_sum, &count_min, &count_max, &min_pos, &max_pos);
        rate_sum = (double)count_sum / time_delta;
        rate_min = (double)count_min / time_delta;
        rate_max = (double)count_max / time_delta;
        rate_avg = rate_sum / (double)_nodes;
        sent_packets = count_sum;
        cout << "Injected packet rate average = " << rate_avg << endl
             << "\tminimum = " << rate_min 
             << " (at node " << min_pos << ")" << endl
             << "\tmaximum = " << rate_max
             << " (at node " << max_pos << ")" << endl;
        _ComputeStats(_accepted_packets[c], &count_sum, &count_min, &count_max, &min_pos, &max_pos);
        rate_sum = (double)count_sum / time_delta;
        rate_min = (double)count_min / time_delta;
        rate_max = (double)count_max / time_delta;
        rate_avg = rate_sum / (double)_nodes;
        accepted_packets = count_sum;
        cout << "Accepted packet rate average = " << rate_avg << endl
             << "\tminimum = " << rate_min 
             << " (at node " << min_pos << ")" << endl
             << "\tmaximum = " << rate_max
             << " (at node " << max_pos << ")" << endl;
        _ComputeStats(_sent_flits[c], &count_sum, &count_min, &count_max, &min_pos, &max_pos);
        rate_sum = (double)count_sum / time_delta;
        rate_min = (double)count_min / time_delta;
        rate_max = (double)count_max / time_delta;
        rate_avg = rate_sum / (double)_nodes;
        sent_flits = count_sum;
        cout << "Injected flit rate average = " << rate_avg << endl
             << "\tminimum = " << rate_min 
             << " (at node " << min_pos << ")" << endl
             << "\tmaximum = " << rate_max
             << " (at node " << max_pos << ")" << endl;
        _ComputeStats(_accepted_flits[c], &count_sum, &count_min, &count_max, &min_pos, &max_pos);
        rate_sum = (double)count_sum / time_delta;
        rate_min = (double)count_min / time_delta;
        rate_max = (double)count_max / time_delta;
        rate_avg = rate_sum / (double)_nodes;
        accepted_flits = count_sum;
        cout << "Accepted flit rate average= " << rate_avg << endl
             << "\tminimum = " << rate_min 
             << " (at node " << min_pos << ")" << endl
             << "\tmaximum = " << rate_max
             << " (at node " << max_pos << ")" << endl;
    
        cout << "Injected packet length average = " << (double)sent_flits / (double)sent_packets << endl
             << "Accepted packet length average = " << (double)accepted_flits / (double)accepted_packets << endl;

        cout << "Total in-flight flits = " << _total_in_flight_flits[c].size()
             << " (" << _measured_in_flight_flits[c].size() << " measured)"
             << endl;
    
#ifdef TRACK_STALLS
        _ComputeStats(_buffer_busy_stalls[c], &count_sum);
        rate_sum = (double)count_sum / time_delta;
        rate_avg = rate_sum / (double)(_subnets*_routers);
        os << "Buffer busy stall rate = " << rate_avg << endl;
        _ComputeStats(_buffer_conflict_stalls[c], &count_sum);
        rate_sum = (double)count_sum / time_delta;
        rate_avg = rate_sum / (double)(_subnets*_routers);
        os << "Buffer conflict stall rate = " << rate_avg << endl;
        _ComputeStats(_buffer_full_stalls[c], &count_sum);
        rate_sum = (double)count_sum / time_delta;
        rate_avg = rate_sum / (double)(_subnets*_routers);
        os << "Buffer full stall rate = " << rate_avg << endl;
        _ComputeStats(_buffer_reserved_stalls[c], &count_sum);
        rate_sum = (double)count_sum / time_delta;
        rate_avg = rate_sum / (double)(_subnets*_routers);
        os << "Buffer reserved stall rate = " << rate_avg << endl;
        _ComputeStats(_crossbar_conflict_stalls[c], &count_sum);
        rate_sum = (double)count_sum / time_delta;
        rate_avg = rate_sum / (double)(_subnets*_routers);
        os << "Crossbar conflict stall rate = " << rate_avg << endl;
#endif
    
    }
}

void TrafficManager::DisplayOverallStats( ostream & os ) const {

    os << "====== Overall Traffic Statistics ======" << endl;
    for ( int c = 0; c < _classes; ++c ) {

        if(_measure_stats[c] == 0) {
            continue;
        }

        os << "====== Traffic class " << c << " ======" << endl;
    
        os << "Packet latency average = " << _overall_avg_plat[c] / (double)_total_sims
           << " (" << _total_sims << " samples)" << endl;
        os << "\tminimum = " << _overall_min_plat[c] / (double)_total_sims
           << " (" << _total_sims << " samples)" << endl;
        os << "\tmaximum = " << _overall_max_plat[c] / (double)_total_sims
           << " (" << _total_sims << " samples)" << endl;

        os << "Network latency average = " << _overall_avg_nlat[c] / (double)_total_sims
           << " (" << _total_sims << " samples)" << endl;
        os << "\tminimum = " << _overall_min_nlat[c] / (double)_total_sims
           << " (" << _total_sims << " samples)" << endl;
        os << "\tmaximum = " << _overall_max_nlat[c] / (double)_total_sims
           << " (" << _total_sims << " samples)" << endl;

        os << "Flit latency average = " << _overall_avg_flat[c] / (double)_total_sims
           << " (" << _total_sims << " samples)" << endl;
        os << "\tminimum = " << _overall_min_flat[c] / (double)_total_sims
           << " (" << _total_sims << " samples)" << endl;
        os << "\tmaximum = " << _overall_max_flat[c] / (double)_total_sims
           << " (" << _total_sims << " samples)" << endl;

        os << "Fragmentation average = " << _overall_avg_frag[c] / (double)_total_sims
           << " (" << _total_sims << " samples)" << endl;
        os << "\tminimum = " << _overall_min_frag[c] / (double)_total_sims
           << " (" << _total_sims << " samples)" << endl;
        os << "\tmaximum = " << _overall_max_frag[c] / (double)_total_sims
           << " (" << _total_sims << " samples)" << endl;

        os << "Injected packet rate average = " << _overall_avg_sent_packets[c] / (double)_total_sims
           << " (" << _total_sims << " samples)" << endl;
        os << "\tminimum = " << _overall_min_sent_packets[c] / (double)_total_sims
           << " (" << _total_sims << " samples)" << endl;
        os << "\tmaximum = " << _overall_max_sent_packets[c] / (double)_total_sims
           << " (" << _total_sims << " samples)" << endl;
 #ifdef EXTRA_STATS    
        os << "Injected packets:" << endl
           << "\tper router [";
        uint64_t totalpackets = 0;
        for(auto i = _accepted_packets[c].begin(); i != _accepted_packets[c].end(); ++i){
            os << (*i) << " ";
            totalpackets += *i;
        }
        os << "]" << std::endl;

        os << "\tper interchiplet link " << endl 
           << _net[c]->printInterChipletPackets() << std::endl;

        os << "\ttotal = " << totalpackets << endl;
#endif
        os << "Accepted packet rate average = " << _overall_avg_accepted_packets[c] / (double)_total_sims
           << " (" << _total_sims << " samples)" << endl;
        os << "\tminimum = " << _overall_min_accepted_packets[c] / (double)_total_sims
           << " (" << _total_sims << " samples)" << endl;
        os << "\tmaximum = " << _overall_max_accepted_packets[c] / (double)_total_sims
           << " (" << _total_sims << " samples)" << endl;

        os << "Injected flit rate average = " << _overall_avg_sent[c] / (double)_total_sims
           << " (" << _total_sims << " samples)" << endl;
        os << "\tminimum = " << _overall_min_sent[c] / (double)_total_sims
           << " (" << _total_sims << " samples)" << endl;
        os << "\tmaximum = " << _overall_max_sent[c] / (double)_total_sims
           << " (" << _total_sims << " samples)" << endl;
    
        os << "Accepted flit rate average = " << _overall_avg_accepted[c] / (double)_total_sims
           << " (" << _total_sims << " samples)" << endl;
        os << "\tminimum = " << _overall_min_accepted[c] / (double)_total_sims
           << " (" << _total_sims << " samples)" << endl;
        os << "\tmaximum = " << _overall_max_accepted[c] / (double)_total_sims
           << " (" << _total_sims << " samples)" << endl;
    
        os << "Injected packet size average = " << _overall_avg_sent[c] / _overall_avg_sent_packets[c]
           << " (" << _total_sims << " samples)" << endl;

        os << "Accepted packet size average = " << _overall_avg_accepted[c] / _overall_avg_accepted_packets[c]
           << " (" << _total_sims << " samples)" << endl;
    
        os << "Hops average = " << _overall_hop_stats[c] / (double)_total_sims
           << " (" << _total_sims << " samples)" << endl;

#ifdef EXTRA_STATS
        os << "Created packets from LLC - src-dst pairs of routers:" << endl;
        for(int src = 0; src < gX*gY; src++){
            os << "\t" << src << ": [" << _createdPacketsLLC[src] << "]" << endl;
        }
        os << "Created packets from rest of caches - src-dst pairs of routers:" << endl;
        for(int src = 0; src < gX*gY; src++){
            os << "\t" << src << ": [" << _createdPacketsRest[src] << "]" << endl;
        }
        os << "Created flits - (src-dst pairs of routers):" << endl
            << "\tper router [ " << _createdFlits << " ] " << endl
            << "\ttotal " << std::accumulate(_createdFlits.begin(), _createdFlits.end(), 0) << endl;

        os << "Injected flits: " << endl
           << "\tper router [ " << _sent_flits << " ] " << endl
           << "\ttotal " << std::accumulate(_sent_flits[c].begin(), _sent_flits[c].end(), 0) << endl;

        os << "Ejected flits TO router: " << endl
           << "\tper router [ "<< _accepted_flits << " ]  " << endl
           << "\ttotal " << std::accumulate(_accepted_flits[c].begin(), _accepted_flits[c].end(), 0) << endl;

        os << "Ejected packets FROM router: " << endl
           << "\tper router [ " << _ejected_packets << " ] " << endl
           << "\ttotal " << std::accumulate(_ejected_packets[c].begin(), _ejected_packets[c].end(), 0) << endl;

        os << "Latency introduced per router: " << endl
            << " [ " << _latency_introduced << " ] " << endl;

        os << "Outstanding flits " << endl
           << "\tper router [ " << outstandingFlits[0] << " ]" << endl
           << "\ttotal " << std::accumulate(outstandingFlits[0].begin(), outstandingFlits[0].end(), 0) << endl;

        os << "\tper interchiplet link " << endl 
           << _net[c]->printInterChipletPackets() << std::endl;
#endif
    
#ifdef TRACK_STALLS
        os << "Buffer busy stall rate = " << (double)_overall_buffer_busy_stalls[c] / (double)_total_sims
           << " (" << _total_sims << " samples)" << endl
           << "Buffer conflict stall rate = " << (double)_overall_buffer_conflict_stalls[c] / (double)_total_sims
           << " (" << _total_sims << " samples)" << endl
           << "Buffer full stall rate = " << (double)_overall_buffer_full_stalls[c] / (double)_total_sims
           << " (" << _total_sims << " samples)" << endl
           << "Buffer reserved stall rate = " << (double)_overall_buffer_reserved_stalls[c] / (double)_total_sims
           << " (" << _total_sims << " samples)" << endl
           << "Crossbar conflict stall rate = " << (double)_overall_crossbar_conflict_stalls[c] / (double)_total_sims
           << " (" << _total_sims << " samples)" << endl;
#endif
    
    }
  
}

string TrafficManager::_OverallStatsCSV(int c) const
{
    ostringstream os;
    os << _traffic[c]
       << ',' << _use_read_write[c]
       << ',' << _load[c]
       << ',' << _overall_min_plat[c] / (double)_total_sims
       << ',' << _overall_avg_plat[c] / (double)_total_sims
       << ',' << _overall_max_plat[c] / (double)_total_sims
       << ',' << _overall_min_nlat[c] / (double)_total_sims
       << ',' << _overall_avg_nlat[c] / (double)_total_sims
       << ',' << _overall_max_nlat[c] / (double)_total_sims
       << ',' << _overall_min_flat[c] / (double)_total_sims
       << ',' << _overall_avg_flat[c] / (double)_total_sims
       << ',' << _overall_max_flat[c] / (double)_total_sims
       << ',' << _overall_min_frag[c] / (double)_total_sims
       << ',' << _overall_avg_frag[c] / (double)_total_sims
       << ',' << _overall_max_frag[c] / (double)_total_sims
       << ',' << _overall_min_sent_packets[c] / (double)_total_sims
       << ',' << _overall_avg_sent_packets[c] / (double)_total_sims
       << ',' << _overall_max_sent_packets[c] / (double)_total_sims
       << ',' << _overall_min_accepted_packets[c] / (double)_total_sims
       << ',' << _overall_avg_accepted_packets[c] / (double)_total_sims
       << ',' << _overall_max_accepted_packets[c] / (double)_total_sims
       << ',' << _overall_min_sent[c] / (double)_total_sims
       << ',' << _overall_avg_sent[c] / (double)_total_sims
       << ',' << _overall_max_sent[c] / (double)_total_sims
       << ',' << _overall_min_accepted[c] / (double)_total_sims
       << ',' << _overall_avg_accepted[c] / (double)_total_sims
       << ',' << _overall_max_accepted[c] / (double)_total_sims
       << ',' << _overall_avg_sent[c] / _overall_avg_sent_packets[c]
       << ',' << _overall_avg_accepted[c] / _overall_avg_accepted_packets[c]
       << ',' << _overall_hop_stats[c] / (double)_total_sims;

#ifdef TRACK_STALLS
    os << ',' << (double)_overall_buffer_busy_stalls[c] / (double)_total_sims
       << ',' << (double)_overall_buffer_conflict_stalls[c] / (double)_total_sims
       << ',' << (double)_overall_buffer_full_stalls[c] / (double)_total_sims
       << ',' << (double)_overall_buffer_reserved_stalls[c] / (double)_total_sims
       << ',' << (double)_overall_crossbar_conflict_stalls[c] / (double)_total_sims;
#endif

    return os.str();
}

void TrafficManager::DisplayOverallStatsCSV(ostream & os) const {
    for(int c = 0; c < _classes; ++c) {
        os << "results:" << c << ',' << _OverallStatsCSV() << endl;
    }
}

//read the watchlist
void TrafficManager::_LoadWatchList(const string & filename){
    ifstream watch_list;
    watch_list.open(filename.c_str());
  
    string line;
    if(watch_list.is_open()) {
        while(!watch_list.eof()) {
            getline(watch_list, line);
            if(line != "") {
                if(line[0] == 'p') {
                    _packets_to_watch.insert(atoi(line.c_str()+1));
                } else {
                    _flits_to_watch.insert(atoi(line.c_str()));
                }
            }
        }
    
    } else {
        Error("Unable to open flit watch file: " + filename);
    }
}

int TrafficManager::_GetNextPacketSize(int cl) const
{
    assert(cl >= 0 && cl < _classes);

    vector<int> const & psize = _packet_size[cl];
    int sizes = psize.size();

    if(sizes == 1) {
        return psize[0];
    }

    vector<int> const & prate = _packet_size_rate[cl];
    int max_val = _packet_size_max_val[cl];

    int pct = RandomInt(max_val);

    for(int i = 0; i < (sizes - 1); ++i) {
        int const limit = prate[i];
        if(limit > pct) {
            return psize[i];
        } else {
            pct -= limit;
        }
    }
    assert(prate.back() > pct);
    return psize.back();
}

double TrafficManager::_GetAveragePacketSize(int cl) const
{
    vector<int> const & psize = _packet_size[cl];
    int sizes = psize.size();
    if(sizes == 1) {
        return (double)psize[0];
    }
    vector<int> const & prate = _packet_size_rate[cl];
    int sum = 0;
    for(int i = 0; i < sizes; ++i) {
        sum += psize[i] * prate[i];
    }
    return (double)sum / (double)(_packet_size_max_val[cl] + 1);
}

void TrafficManager::printInjectedPackets() {
    bool firstIteration = true;
    int cnt_packs = 0;
    for(int inodes = 0; inodes < _nodes; ++inodes) {
        for(int jclasses = 0; jclasses < _classes; ++jclasses) {
            if (! _partial_packets[inodes][jclasses].empty()){// & (inodes == 15)){
                if (firstIteration){
                    firstIteration = false;
                    printf("%d == _partial_packets    +++++++++++++++++++++++++++++++++++++++++++++\n",cnt);
                }
                for (auto packet : _partial_packets[inodes][jclasses]){
                    cout << *packet << endl;
                    cnt_packs++;
                }
            }
        }
    }
    cout << "++++++++++++++++++++++++++++++++++++++++++" << endl <<
    "total partial packets: " << cnt_packs << endl << 
     "++++++++++++++++++++++++++++++++++++++++++" << endl;

     int cnt_flits = 0;
    firstIteration = true;
    for(int jclasses = 0; jclasses < _classes; ++jclasses) {
        if (!_total_in_flight_flits[jclasses].empty()){
            if (firstIteration){
                firstIteration = false;
                printf("%d == _total_in_flights   ============================================\n", cnt);
            }
            
            for(auto it = _total_in_flight_flits[jclasses].cbegin(); 
                        it != _total_in_flight_flits[jclasses].cend(); ++it){
                // if ((it->second)->src == 15){
                    cout << *it->second << endl; 
                    cnt_flits++;
                // }
            }
        }
    }
    cout << "++++++++++++++++++++++++++++++++++++++++++" << endl <<
    "total fligh flits: " << cnt_flits << endl << 
     "++++++++++++++++++++++++++++++++++++++++++" << endl;

    cnt_flits = 0;
    firstIteration = true;
    for(int jclasses = 0; jclasses < _classes; ++jclasses) {
        if (!_measured_in_flight_flits[jclasses].empty()){
            if (firstIteration){
                firstIteration = false;
                // printf("%d == _total_in_flights   ============================================\n", cnt);
            }
            
            for(auto it = _measured_in_flight_flits[jclasses].cbegin(); 
                        it != _measured_in_flight_flits[jclasses].cend(); ++it){
                // if ((it->second)->src == 15){
                    cout << *it->second << endl; 
                    cnt_flits++;
                // }
            }
        }
    }
    cout << "++++++++++++++++++++++++++++++++++++++++++" << endl <<
    "total _measured_in_flight_flits: " << cnt_flits << endl << 
     "++++++++++++++++++++++++++++++++++++++++++" << endl;
}

uint64_t TrafficManager::_ManuallyGeneratePacket(int source, int dest, int size, simTime ctime, uint64_t addr, bool llcEvent, BookSimNetwork *nocAddr){
    // The packets here are used by zsim, so no warmup stage is needed.
    // In running stage, record is always one and the packets are also
    // inserted in the _measured_in_flight_flits vector as well.
    if (ctime < 0){
        ctime = _time;
    }
    int result = _injection_process[0]->test(source) ? 1 : 0;
    _requestsOutstanding[source]++;

    if (result != 0){
        _packet_seq_no[source]++;
    }


    int cl = 0;

    Flit::FlitType packet_type = Flit::ANY_TYPE;
    // int size = _GetNextPacketSize(cl); //input size 
    uint64_t pid = _cur_pid++;

    _in_flight_req_address.insert(make_pair(pid,make_pair(nocAddr,addr) ));
    assert(_cur_pid);
    bool record = true; 

#ifdef PRINT_ALL //print all packets
    bool watch = 1;
#else
    bool watch =  gWatchOut && (_packets_to_watch.count(pid) > 0);
#endif
    int subnetwork = ((packet_type == Flit::ANY_TYPE) ? 
                      RandomInt(_subnets-1) :
                      _subnet[packet_type]);
  

#if defined(_SKIP_STEP_) || defined(_EMPTY_STEP_)
    // save the pid of the generated PACKET and the zll.
    // we do not really need any other information
    int src_x = source/gX, src_y = source%gX;
    int dst_x = dest/gX, dst_y = dest%gX;
    int zll = (abs(dst_x-src_x) + abs(dst_y-src_y) + 1)*(4) + (5-1) + 50; // hoplatency = 4, flits/packet = 5, hack = 3

    _in_flight_packets.insert(make_pair(pid, zll));
#else


    for ( int i = 0; i < size; ++i ) { // input size
        Flit * f  = Flit::New(); //generate a new flit 
        f->id     = _cur_id++;
        assert(_cur_id);
        f->pid    = pid;
        f->watch  = watch | (gWatchOut && (_flits_to_watch.count(f->id) > 0));
        f->subnetwork = subnetwork;
        f->src    = source;
        f->ctime  = ctime; // the packet carries the _time that was created 
        f->record = record;
        f->cl     = cl;
        f->llcEvent = llcEvent;


        //contains all the newly generated flits. 
        // Note that this assignment happens BEFORE the f->head, f->dest, f->pri etc  are set
        _total_in_flight_flits[f->cl].insert(make_pair(f->id, f)); 
        if(record) { 
            _measured_in_flight_flits[f->cl].insert(make_pair(f->id, f));
            #ifdef CALC_INJECTION_RATE
            cnt_msr_flits[f->src]++;
            cnt_msr_flit_total++;
            #endif
        }
    
        if(gTrace){
            cout<<"New Flit. Src:"<<f->src << 
            ", packet: " << f->pid <<
            ", id: " << f->id <<
            ", time: " << f->ctime << endl;
        }
        
        f->type = packet_type;

        if ( i == 0 ) { // Head flit
            f->head = true;
            //packets are only generated to nodes smaller or equal to limit
            f->dest = dest;
        } else {
            f->head = false;
            f->dest = -1; // destination -1 when body flit
        }
        f->pri = 0;
        
        if ( i == ( size - 1 ) ) { // Tail flit
            f->tail = true;
        } else {
            f->tail = false;
        }
    
        f->vc  = -1;
        // note that push_back puts the element at the end of the vector
        _partial_packets[source][cl].push_back( f ); //Adds a new element at the end of the vector, after its current last element.

        #ifdef CALC_INJECTION_RATE
        cnt_gen_flits[source]++;
        cnt_gen_flit_total++;
        #endif
    }

#ifdef EXTRA_STATS
    ++_createdPackets[source][dest];
    _createdFlits[source] += size;
    if(llcEvent){
        ++_createdPacketsLLC[source][dest];
    } else {
        ++_createdPacketsRest[source][dest];
    }
#endif
    outstandingFlits[subnetwork][source] += size;
    return pid;
#endif
}
