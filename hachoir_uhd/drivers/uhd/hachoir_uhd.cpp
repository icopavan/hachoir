//
// Copyright 2010-2011,2014 Ettus Research LLC
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//

#include <uhd/types/tune_request.hpp>
#include <uhd/utils/thread_priority.hpp>
#include <uhd/utils/safe_main.hpp>
#include <uhd/usrp/multi_usrp.hpp>
#include <uhd/exception.hpp>
#include <boost/program_options.hpp>
#include <boost/format.hpp>
#include <boost/thread.hpp>
#include <iostream>
#include <fstream>
#include <csignal>
#include <complex>

#include "utils/rxtimedomain.h"

namespace po = boost::program_options;

static bool stop_signal_called = false;
void sig_int_handler(int){stop_signal_called = true;}

bool RX_msg_cb(const Message &msg, phy_parameters_t &phy, void *userData)
{
	return true;
}

template<typename samp_type> bool recv_to_file(
    uhd::usrp::multi_usrp::sptr usrp,
    phy_parameters_t &phy,
    const std::string &cpu_format,
    const std::string &wire_format,
    const std::string &file,
    size_t samps_per_buff,
    unsigned long long num_requested_samples,
    double time_requested = 0.0,
    bool bw_summary = false,
    bool stats = false,
    bool null = false,
    bool enable_size_map = false,
    bool continue_on_bad_packet = false
){
	unsigned long long num_total_samps = 0;
	//create a receive streamer
	uhd::stream_args_t stream_args(cpu_format,wire_format);
	uhd::rx_streamer::sptr rx_stream = usrp->get_rx_stream(stream_args);

	uhd::rx_metadata_t md;
	std::vector<samp_type> buff(samps_per_buff);
	std::ofstream outfile;
	if (not null)
		outfile.open(file.c_str(), std::ofstream::binary);
	bool overflow_message = true;

	//setup streaming
	uhd::stream_cmd_t stream_cmd((num_requested_samples == 0)?
	uhd::stream_cmd_t::STREAM_MODE_START_CONTINUOUS:
	uhd::stream_cmd_t::STREAM_MODE_NUM_SAMPS_AND_DONE
	);
	stream_cmd.num_samps = num_requested_samples;
	stream_cmd.stream_now = true;
	stream_cmd.time_spec = uhd::time_spec_t();
	rx_stream->issue_stream_cmd(stream_cmd);

	boost::system_time start = boost::get_system_time();
	unsigned long long ticks_requested = (long)(time_requested * (double)boost::posix_time::time_duration::ticks_per_second());
	boost::posix_time::time_duration ticks_diff;
	boost::system_time last_update = start;
	unsigned long long last_update_samps = 0;

	typedef std::map<size_t,size_t> SizeMap;
	SizeMap mapSizes;

	RXTimeDomain rxTimeDomain(RX_msg_cb, NULL);
	rxTimeDomain.setPhyParameters(phy);

	while(not stop_signal_called and (num_requested_samples != num_total_samps or num_requested_samples == 0)) {
		boost::system_time now = boost::get_system_time();
		size_t num_rx_samps = rx_stream->recv(&buff.front(), buff.size(), md, 3.0, enable_size_map);

		if (md.error_code == uhd::rx_metadata_t::ERROR_CODE_TIMEOUT) {
			std::cout << boost::format("Timeout while streaming") << std::endl;
			break;
		}
		if (md.error_code == uhd::rx_metadata_t::ERROR_CODE_OVERFLOW){
			if (overflow_message){
				overflow_message = false;
				std::cerr << boost::format(
				    "Got an overflow indication. Please consider the following:\n"
				    "  Your write medium must sustain a rate of %fMB/s.\n"
				    "  Dropped samples will not be written to the file.\n"
				    "  Please modify this example for your purposes.\n"
				    "  This message will not appear again.\n"
				) % (usrp->get_rx_rate()*sizeof(samp_type)/1e6);
			}
			continue;
		}
		if (md.error_code != uhd::rx_metadata_t::ERROR_CODE_NONE){
			std::string error = str(boost::format("Unexpected error code 0x%x") % md.error_code);
			if (continue_on_bad_packet){
				std::cerr << error << std::endl;
				continue;
			}
			else
				throw std::runtime_error(error);
		}

		if (enable_size_map){
			SizeMap::iterator it = mapSizes.find(num_rx_samps);
			if (it == mapSizes.end())
				mapSizes[num_rx_samps] = 0;
			mapSizes[num_rx_samps] += 1;
		}

		num_total_samps += num_rx_samps;

		if (outfile.is_open())
			outfile.write((const char*)&buff.front(), num_rx_samps*sizeof(samp_type));

		if (rxTimeDomain.processSamples(md.time_spec.to_ticks(1000000),
						buff.data(), num_rx_samps)) {
			//tear-down streaming
			uhd::stream_cmd_t stream_cmd(uhd::stream_cmd_t::STREAM_MODE_STOP_CONTINUOUS);
			stream_cmd.stream_now = true;
			stream_cmd.time_spec = uhd::time_spec_t();
			rx_stream->issue_stream_cmd(stream_cmd);

			return true;
		}

		if (bw_summary){
			last_update_samps += num_rx_samps;
			boost::posix_time::time_duration update_diff = now - last_update;
			if (update_diff.ticks() > boost::posix_time::time_duration::ticks_per_second()) {
				double t = (double)update_diff.ticks() / (double)boost::posix_time::time_duration::ticks_per_second();
				double r = (double)last_update_samps / t;
				std::cout << boost::format("\t%f Msps") % (r/1e6) << std::endl;
				last_update_samps = 0;
				last_update = now;
			}
		}

		ticks_diff = now - start;
		if (ticks_requested > 0){
			if ((unsigned long long)ticks_diff.ticks() > ticks_requested)
				break;
		}
	}

	if (outfile.is_open())
		outfile.close();

	if (stats){
		std::cout << std::endl;

		double t = (double)ticks_diff.ticks() / (double)boost::posix_time::time_duration::ticks_per_second();
		std::cout << boost::format("Received %d samples in %f seconds") % num_total_samps % t << std::endl;
		double r = (double)num_total_samps / t;
		std::cout << boost::format("%f Msps") % (r/1e6) << std::endl;

		if (enable_size_map) {
			std::cout << std::endl;
			std::cout << "Packet size map (bytes: count)" << std::endl;
			for (SizeMap::iterator it = mapSizes.begin(); it != mapSizes.end(); it++)
				std::cout << it->first << ":\t" << it->second << std::endl;
		}
	}

	return false;
}

typedef boost::function<uhd::sensor_value_t (const std::string&)> get_sensor_fn_t;

bool usrp_set_phy(uhd::usrp::multi_usrp::sptr usrp, const phy_parameters_t &phy,
		  const std::string &ref, bool int_n_tuning, bool check_lo,
		  float lo_timeout)
{
	//set the center frequency
	std::cout << boost::format("Setting RX Freq: %f MHz...") % (phy.central_freq/1e6) << std::endl;
	uhd::tune_request_t tune_request(phy.central_freq);
	if(int_n_tuning) tune_request.args = uhd::device_addr_t("mode_n=integer");
	uhd::tune_result_t ret = usrp->set_rx_freq(tune_request);
	std::cout << boost::format("Actual RX Freq: %f MHz...") % (usrp->get_rx_freq()/1e6) << std::endl << std::endl;

	float tune_err = fabs(ret.target_rf_freq - ret.actual_rf_freq) / ret.target_rf_freq;
	if (tune_err > 0.01) {
		std::cerr << "Set_PHY: Tune error > 1% (" << tune_err * 100 << " %), abort" << std::endl;
		return false;
	}

	//set the sample rate
	if (phy.sample_rate <= 0.0){
		std::cerr << "Please specify a valid sample rate" << std::endl;
		return false;
	}
	std::cout << boost::format("Setting RX Rate: %f Msps...") % (phy.sample_rate/1e6) << std::endl;
	usrp->set_rx_rate(phy.sample_rate);
	std::cout << boost::format("Actual RX Rate: %f Msps...") % (usrp->get_rx_rate()/1e6) << std::endl << std::endl;

	//set the rf gain
	if (phy.gain >= 0.0) {
		std::cout << boost::format("Setting RX Gain: %f dB...") % phy.gain << std::endl;
		usrp->set_rx_gain(phy.gain);
		std::cout << boost::format("Actual RX Gain: %f dB...") % usrp->get_rx_gain() << std::endl << std::endl;
	}

	//set the IF filter bandwidth
	if (phy.IF_bw >= 0.0) {
		std::cout << boost::format("Setting RX Bandwidth: %f MHz...") % phy.IF_bw << std::endl;
		usrp->set_rx_bandwidth(phy.IF_bw);
		std::cout << boost::format("Actual RX Bandwidth: %f MHz...") % usrp->get_rx_bandwidth() << std::endl << std::endl;
	}

	// wait for lo_locked */
	std::cout << "Waiting on the LO: ";
	int i = 0;
	while (not usrp->get_rx_sensor("lo_locked").to_bool()){
		boost::this_thread::sleep(boost::posix_time::milliseconds(1));
		i++;
		if (i % 100)
			std::cout << "+ ";
		if (i > 1000) {
			std::cout << "failed!" << std::endl;
			return false;
		}
	}
	std::cout << "locked!" << std::endl << std::endl;

	return true;
}

int UHD_SAFE_MAIN(int argc, char *argv[]){
	uhd::set_thread_priority_safe();

	phy_parameters_t phy;

	//variables to be set by po
	std::string args, file, ant, subdev, ref, wirefmt;
	size_t total_num_samps, spb;
	double total_time, setup_time;

    //setup the program options
	po::options_description desc("Allowed options");
	desc.add_options()
		("help", "help message")
		("args", po::value<std::string>(&args)->default_value(""), "multi uhd device address args")
		("file", po::value<std::string>(&file)->default_value(""), "name of the file to write binary samples to")
		("nsamps", po::value<size_t>(&total_num_samps)->default_value(0), "total number of samples to receive")
		("time", po::value<double>(&total_time)->default_value(0), "total number of seconds to receive")
		("spb", po::value<size_t>(&spb)->default_value(1000), "samples per buffer")
		("rate", po::value<float>(&phy.sample_rate)->default_value(1e6), "rate of incoming samples")
		("freq", po::value<float>(&phy.central_freq)->default_value(0.0), "RF center frequency in Hz")
		("gain", po::value<float>(&phy.gain), "gain for the RF chain")
		("ant", po::value<std::string>(&ant), "daughterboard antenna selection")
		("subdev", po::value<std::string>(&subdev), "daughterboard subdevice specification")
		("bw", po::value<float>(&phy.IF_bw), "daughterboard IF filter bandwidth in Hz")
		("ref", po::value<std::string>(&ref)->default_value("internal"), "waveform type (internal, external, mimo)")
		("wirefmt", po::value<std::string>(&wirefmt)->default_value("sc16"), "wire format (sc8 or sc16)")
		("setup", po::value<double>(&setup_time)->default_value(5.0), "seconds of setup time")
		("progress", "periodically display short-term bandwidth")
		("stats", "show average bandwidth on exit")
		("sizemap", "track packet size and display breakdown on exit")
		("null", "run without writing to file")
		("continue", "don't abort on a bad packet")
		("skip-lo", "skip checking LO lock status")
		("int-n", "tune USRP with integer-N tuning")
	;
	po::variables_map vm;
	po::store(po::parse_command_line(argc, argv, desc), vm);
	po::notify(vm);

	//print the help message
	if (vm.count("help")){
	std::cout << boost::format("Hachoir UHD %s") % desc << std::endl;
	return ~0;
	}

	bool bw_summary = vm.count("progress") > 0;
	bool stats = vm.count("stats") > 0;
	bool null = vm.count("null") > 0;
	bool enable_size_map = vm.count("sizemap") > 0;
	bool continue_on_bad_packet = vm.count("continue") > 0;

	if (not vm.count("bw"))
		phy.IF_bw = -1.0;
	if (not vm.count("gain"))
		phy.gain = -1.0;

	if (enable_size_map)
		std::cout << "Packet size tracking enabled - will only recv one packet at a time!" << std::endl;

	//create a usrp device
	std::cout << std::endl;
	std::cout << boost::format("Creating the usrp device with: %s...") % args << std::endl;
	uhd::usrp::multi_usrp::sptr usrp = uhd::usrp::multi_usrp::make(args);

	//Lock mboard clocks
	usrp->set_clock_source(ref);

	//always select the subdevice first, the channel mapping affects the other settings
	if (vm.count("subdev")) usrp->set_rx_subdev_spec(subdev);

	std::cout << boost::format("Using Device: %s") % usrp->get_pp_string() << std::endl;

	//set the antenna
	if (vm.count("ant")) usrp->set_rx_antenna(ant);

	if (total_num_samps == 0){
		std::signal(SIGINT, &sig_int_handler);
		std::signal(SIGTERM, &sig_int_handler);
		std::signal(SIGQUIT, &sig_int_handler);
		std::signal(SIGABRT, &sig_int_handler);
		std::cout << "Press Ctrl + C to stop streaming..." << std::endl << std::endl;
	}

#define recv_to_file_args(format) \
	(usrp, phy, format, wirefmt, file, spb, total_num_samps, total_time, bw_summary, stats, null, enable_size_map, continue_on_bad_packet)

	bool phy_ok, start_over;

	do {
		phy_ok = usrp_set_phy(usrp, phy, ref, vm.count("int-n"), not vm.count("skip-lo"), setup_time);
		if (!phy_ok)
			continue;

		//recv to file
		start_over = recv_to_file<std::complex<short> >recv_to_file_args("sc16");

		//finished
		std::cout << std::endl << "Done!" << std::endl << std::endl;
	} while (phy_ok && start_over);

	return EXIT_SUCCESS;
}
