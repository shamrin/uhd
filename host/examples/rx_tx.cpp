//
// Copyright 2011-2012,2014 Ettus Research LLC
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
#include <boost/program_options.hpp>
#include <boost/format.hpp>
#include <boost/thread.hpp>
#include <iostream>
#include <fstream>
#include <complex>
#include <csignal>

namespace po = boost::program_options;

static bool stop_signal_called = false;
void sig_int_handler(int){stop_signal_called = true;}

template<typename samp_type> void loopback(
    uhd::usrp::multi_usrp::sptr usrp,
    const std::string &cpu_format,
    const std::string &wire_format,
    const std::string &file,
    size_t samps_per_buff
){

    //create a transmit streamer
    uhd::stream_args_t stream_args(cpu_format, wire_format);
    uhd::tx_streamer::sptr tx_stream = usrp->get_tx_stream(stream_args);

    uhd::tx_metadata_t md;
    md.start_of_burst = false;
    md.end_of_burst = false;
    std::vector<samp_type> buff(samps_per_buff);
    std::ifstream infile(file.c_str(), std::ifstream::binary);

    //loop until the entire file has been read

    while(not md.end_of_burst and not stop_signal_called){

        infile.read((char*)&buff.front(), buff.size()*sizeof(samp_type));
        size_t num_tx_samps = infile.gcount()/sizeof(samp_type);

        md.end_of_burst = infile.eof();

        tx_stream->send(&buff.front(), num_tx_samps, md);
    }

    infile.close();
}

int UHD_SAFE_MAIN(int argc, char *argv[]){
    uhd::set_thread_priority_safe();

    //variables to be set by po
    std::string args, /* del */ file, type, rx_ant, tx_ant, rx_subdev, tx_subdev, ref, wirefmt;
    size_t spb;
    double rate, rx_freq, tx_freq, rx_gain, tx_gain, bw, /* del */ delay, lo_off;

    //setup the program options
    po::options_description desc("Allowed options");
    desc.add_options()
        ("help", "help message")
        ("args", po::value<std::string>(&args)->default_value(""), "multi uhd device address args")
        /* del */ ("file", po::value<std::string>(&file)->default_value("usrp_samples.dat"), "name of the file to read binary samples from")
        ("type", po::value<std::string>(&type)->default_value("short"), "sample type: double, float, or short")
        ("spb", po::value<size_t>(&spb)->default_value(10000), "samples per buffer")
        ("rate", po::value<double>(&rate), "rate of incoming and outgoing samples")
        ("tx-freq", po::value<double>(&tx_freq), "TX RF center frequency in Hz")
        ("rx-freq", po::value<double>(&rx_freq), "RX RF center frequency in Hz")
        ("lo_off", po::value<double>(&lo_off), "Offset for frontend LO in Hz (optional)")
        ("tx-gain", po::value<double>(&tx_gain), "TX gain for the RF chain")
        ("rx-gain", po::value<double>(&rx_gain), "RX gain for the RF chain")
        ("tx-ant", po::value<std::string>(&tx_ant), "TX antenna selection")
        ("rx-ant", po::value<std::string>(&rx_ant), "RX antenna selection")
        ("tx-subdev", po::value<std::string>(&tx_subdev), "TX subdevice specification")
        ("rx-subdev", po::value<std::string>(&rx_subdev), "RX subdevice specification")
        ("bw", po::value<double>(&bw), "analog frontend filter bandwidth in Hz")
        ("ref", po::value<std::string>(&ref)->default_value("internal"), "reference source (internal, external, mimo)")
        ("wirefmt", po::value<std::string>(&wirefmt)->default_value("sc16"), "wire format (sc8 or sc16)")
        /* del */ ("delay", po::value<double>(&delay)->default_value(0.0), "specify a delay between repeated transmission of file")
        /* del */ ("repeat", "repeatedly transmit file")
        ("int-n", "tune USRP with integer-n tuning")
    ;
    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);

    //print the help message
    if (vm.count("help")){
        std::cout << boost::format("UHD RX samples and TX them back (loopback) %s") % desc << std::endl;
        return ~0;
    }

    /* del */ bool repeat = vm.count("repeat");

    //create a usrp device
    std::cout << std::endl;
    std::cout << boost::format("Creating the usrp device with: %s...") % args << std::endl;
    uhd::usrp::multi_usrp::sptr usrp = uhd::usrp::multi_usrp::make(args);

    //Lock mboard clocks
    usrp->set_clock_source(ref);

    //always select the subdevice first, the channel mapping affects the other settings
    if (vm.count("tx-subdev")) usrp->set_tx_subdev_spec(tx_subdev);
    if (vm.count("rx-subdev")) usrp->set_rx_subdev_spec(rx_subdev);

    std::cout << boost::format("Using Device: %s") % usrp->get_pp_string() << std::endl;

    //set the sample rate
    if (not vm.count("rate")){
        std::cerr << "Please specify the sample rate with --rate" << std::endl;
        return ~0;
    }
    std::cout << boost::format("Setting TX Rate: %f Msps...") % (rate/1e6) << std::endl;
    usrp->set_tx_rate(rate);
    std::cout << boost::format("Actual TX Rate: %f Msps...") % (usrp->get_tx_rate()/1e6) << std::endl << std::endl;

    //set TX center frequency
    if (not vm.count("tx-freq")){
        std::cerr << "Please specify the TX center frequency with --tx-freq" << std::endl;
        return ~0;
    }
    std::cout << boost::format("Setting TX Freq: %f MHz...") % (tx_freq/1e6) << std::endl;
    uhd::tune_request_t tx_tune_request;
    if(vm.count("lo_off")) tx_tune_request = uhd::tune_request_t(tx_freq, lo_off);
    else tx_tune_request = uhd::tune_request_t(tx_freq);
    if(vm.count("int-n")) tx_tune_request.args = uhd::device_addr_t("mode_n=integer");
    usrp->set_tx_freq(tx_tune_request);
    std::cout << boost::format("Actual TX Freq: %f MHz...") % (usrp->get_tx_freq()/1e6) << std::endl << std::endl;

    //set RX center frequency
    if (not vm.count("rx-freq")){
        std::cerr << "Please specify the RX center frequency with --rx-freq" << std::endl;
        return ~0;
    }
    std::cout << boost::format("Setting RX Freq: %f MHz...") % (rx_freq/1e6) << std::endl;
    uhd::tune_request_t rx_tune_request(rx_freq);
    if(vm.count("int-n")) rx_tune_request.args = uhd::device_addr_t("mode_n=integer");
    usrp->set_rx_freq(rx_tune_request);
    std::cout << boost::format("Actual RX Freq: %f MHz...") % (usrp->get_rx_freq()/1e6) << std::endl << std::endl;

    //set rf gains
    if (vm.count("tx-gain")){
        std::cout << boost::format("Setting TX Gain: %f dB...") % tx_gain << std::endl;
        usrp->set_tx_gain(tx_gain);
        std::cout << boost::format("Actual TX Gain: %f dB...") % usrp->get_tx_gain() << std::endl << std::endl;
    }
    if (vm.count("rx-gain")) {
        std::cout << boost::format("Setting RX Gain: %f dB...") % rx_gain << std::endl;
        usrp->set_rx_gain(rx_gain);
        std::cout << boost::format("Actual RX Gain: %f dB...") % usrp->get_rx_gain() << std::endl << std::endl;
    }

    if (vm.count("bw")){
        //set the analog frontend filter bandwidth
        std::cout << boost::format("Setting TX Bandwidth: %f MHz...") % bw << std::endl;
        usrp->set_tx_bandwidth(bw);
        std::cout << boost::format("Actual TX Bandwidth: %f MHz...") % usrp->get_tx_bandwidth() << std::endl << std::endl;
        //set the IF filter bandwidth
        std::cout << boost::format("Setting RX Bandwidth: %f MHz...") % bw << std::endl;
        usrp->set_rx_bandwidth(bw);
        std::cout << boost::format("Actual RX Bandwidth: %f MHz...") % usrp->get_rx_bandwidth() << std::endl << std::endl;
    }

    //set antennas
    if (vm.count("tx-ant")) usrp->set_tx_antenna(tx_ant);
    if (vm.count("rx-ant")) usrp->set_rx_antenna(rx_ant);

    boost::this_thread::sleep(boost::posix_time::seconds(1)); //allow for some setup time

    //Check Ref and LO Lock detect
    std::vector<std::string> sensor_names;
    sensor_names = usrp->get_tx_sensor_names(0);
    if (std::find(sensor_names.begin(), sensor_names.end(), "lo_locked") != sensor_names.end()) {
        uhd::sensor_value_t lo_locked = usrp->get_tx_sensor("lo_locked",0);
        std::cout << boost::format("Checking TX: %s ...") % lo_locked.to_pp_string() << std::endl;
        UHD_ASSERT_THROW(lo_locked.to_bool());
    }
    sensor_names = usrp->get_rx_sensor_names(0);
    if (std::find(sensor_names.begin(), sensor_names.end(), "lo_locked") != sensor_names.end()) {
        uhd::sensor_value_t lo_locked = usrp->get_rx_sensor("lo_locked",0);
        std::cout << boost::format("Checking RX: %s ...") % lo_locked.to_pp_string() << std::endl;
        UHD_ASSERT_THROW(lo_locked.to_bool());
    }
    sensor_names = usrp->get_mboard_sensor_names(0);
    if ((ref == "mimo") and (std::find(sensor_names.begin(), sensor_names.end(), "mimo_locked") != sensor_names.end())) {
        uhd::sensor_value_t mimo_locked = usrp->get_mboard_sensor("mimo_locked",0);
        std::cout << boost::format("Checking: %s ...") % mimo_locked.to_pp_string() << std::endl;
        UHD_ASSERT_THROW(mimo_locked.to_bool());
    }
    if ((ref == "external") and (std::find(sensor_names.begin(), sensor_names.end(), "ref_locked") != sensor_names.end())) {
        uhd::sensor_value_t ref_locked = usrp->get_mboard_sensor("ref_locked",0);
        std::cout << boost::format("Checking: %s ...") % ref_locked.to_pp_string() << std::endl;
        UHD_ASSERT_THROW(ref_locked.to_bool());
    }

    //set sigint if user wants to receive
    if(repeat){
        std::signal(SIGINT, &sig_int_handler);
        std::cout << "Press Ctrl + C to stop streaming..." << std::endl;
    }

    //send from file
    do{
        if (type == "double") loopback<std::complex<double> >(usrp, "fc64", wirefmt, file, spb);
        else if (type == "float") loopback<std::complex<float> >(usrp, "fc32", wirefmt, file, spb);
        else if (type == "short") loopback<std::complex<short> >(usrp, "sc16", wirefmt, file, spb);
        else throw std::runtime_error("Unknown type " + type);

        /* del */ if(repeat and delay != 0.0) boost::this_thread::sleep(boost::posix_time::milliseconds(delay));
    } while(repeat and not stop_signal_called);

    //finished
    std::cout << std::endl << "Done!" << std::endl << std::endl;

    return EXIT_SUCCESS;
}