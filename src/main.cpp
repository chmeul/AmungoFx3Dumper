#include <iostream>
#include <chrono>
#include <inttypes.h>

#include "hwfx3/fx3dev.h"

#ifdef WIN32
#include "hwfx3/fx3devcyapi.h"
#endif

#include "processors/streamdumper.h"

using namespace std;

int main( int argn, const char** argv )
{
#ifndef WIN32
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);
#endif

    cerr << "*** Amungo's dumper for nut4nt board ***" << endl << endl;
    if ( argn != 6 ) {
        cerr << "Usage: "
             << "AmungoFx3Dumper"     << " "
             << "FX3_IMAGE"           << " "
             << "NT1065_CFG"          << " "
             << " OUT_FILE | stdout " << " "
             << " SECONDS | inf"      << " "
             << "cypress | libusb"
             << endl << endl;

        cerr << "Use 'stdout'' as a file name to direct signal to standart output stream" << endl;
        cerr << endl;

        cerr << "Example (dumping one minute of signal and use cypress driver):" << endl;
        cerr << "AmungoFx3Dumper AmungoItsFx3Firmware.img  nt1065.hex  dump4ch.bin  60  cypress" << endl;
        cerr << endl;

        cerr << "Example (dumping signal non-stop to stdout):" << endl;
        cerr << "AmungoFx3Dumper AmungoItsFx3Firmware.img  nt1065.hex  stdout  inf  libusb" << endl;
        cerr << endl;
        return 0;
    }

    std::string fximg( argv[1] );
    std::string ntcfg( argv[2] );
    std::string dumpfile( argv[3] );

    double seconds = 0.0;
    const double INF_SECONDS = 10.0 * 365.0 * 24.0 * 60.0 * 60.0;
    if ( string(argv[4]) == string("inf") ) {
        seconds = INF_SECONDS;
    } else {
        seconds = atof( argv[4] );
    }

    std::string driver( argv[5] );

    bool useCypress = ( driver == string( "cypress" ) );

    cerr << "------------------------------" << endl;
    if ( seconds >= INF_SECONDS ) {
        cerr << "Dump non-stop to " << dumpfile << endl;
    } else if ( seconds > 0.0 ) {
        cerr << "Dump " << seconds << " seconds to '" << dumpfile << "'" << endl;
    } else {
        cerr << "No dumping - just testing!" << endl;
    }
    cerr << "Using fx3 image from '" << fximg << "' and nt1065 config from '" << ntcfg << "'" << endl;
    cerr << "You choose to use __" << ( useCypress ? "cypress" : "libusb" ) << "__ driver" << endl;
    cerr << "------------------------------" << endl;

    cerr << "Wait while device is being initing..." << endl;
    FX3DevIfce* dev = nullptr;

#ifdef WIN32
    if ( useCypress ) {
        dev = new FX3DevCyAPI();
    } else {
        dev = new FX3Dev( 2 * 1024 * 1024, 7 );
    }
#else
    if ( useCypress ) {
        cerr << endl
             << "WARNING: you can't use cypress driver under Linux."
             << " Please check if you use correct scripts!"
             << endl;
    }
    dev = new FX3Dev( 2 * 1024 * 1024, 7 );
#endif

    if ( dev->init(fximg.c_str(), ntcfg.c_str() ) != FX3_ERR_OK ) {
        cerr << endl << "Problems with hardware or driver type" << endl;
        return -1;
    }
    cerr << "Device was inited." << endl << endl;

    std::this_thread::sleep_for(chrono::milliseconds(1000));

    cerr << "Determinating sample rate";
    if ( !seconds ) {
        cerr << " and USB noise level...";
    }
    cerr << endl;

    dev->startRead(nullptr);

    // This is temporary workaround for strange bug of 'odd launch'
    std::this_thread::sleep_for(chrono::milliseconds(100));
    dev->stopRead();
    std::this_thread::sleep_for(chrono::milliseconds(100));
    dev->startRead(nullptr);

    std::this_thread::sleep_for(chrono::milliseconds(1000));

    dev->getDebugInfoFromBoard(false);

    double size_mb = 0.0;
    double phy_errs = 0;
    int sleep_ms = 200;
    int iter_cnt = 5;
    double overall_seconds = ( sleep_ms * iter_cnt ) / 1000.0;
    fx3_dev_debug_info_t info = dev->getDebugInfoFromBoard();
    for ( int i = 0; i < iter_cnt; i++ ) {
        std::this_thread::sleep_for(chrono::milliseconds(sleep_ms));
        info = dev->getDebugInfoFromBoard();
        //info.print();
        cerr << ".";
        size_mb += info.size_tx_mb_inc;
        phy_errs += info.phy_err_inc;
    }
    cerr << endl;

    int64_t CHIP_SR = (int64_t)((size_mb * 1024.0 * 1024.0 )/overall_seconds);

    cerr << endl;
    cerr << "SAMPLE RATE  is ~" << CHIP_SR / 1000000 << " MHz " << endl;
    if ( !seconds ) {
        cerr << "NOISE  LEVEL is  " << phy_errs / size_mb << " noisy packets per one megabyte" << endl;
    }
    cerr << endl;
    std::this_thread::sleep_for(chrono::milliseconds(1000));


    const int64_t bytes_per_sample = 1;
    int64_t bytes_to_dump = (int64_t)( CHIP_SR * seconds * bytes_per_sample );
    uint32_t overs_cnt_at_start = info.overflows;

    if ( bytes_to_dump ) {
        cerr << "Start dumping data" << endl;
    } else {
        cerr << "Start testing USB transfer" << endl;
    }
    StreamDumper* dumper = nullptr;
    int32_t iter_time_ms = 2000;
    thread poller;
    bool poller_running = true;
    try {

        dumper = new StreamDumper( dumpfile, bytes_to_dump );
        if ( bytes_to_dump ) {
            dev->changeHandler(dumper);
        } else {
            dev->changeHandler(nullptr);
        }

        auto start_time = chrono::system_clock::now();


        poller = thread( [&]() {
            FILE* flog = fopen( "regdump.txt", "w" );
            while ( poller_running ) {
                uint8_t wr_val;
                uint8_t rd_val[6];

                for ( int ch = 0; ch < 4 && poller_running; ch++ ) {
                    wr_val = ( ( ch << 4 ) | ( 0x0 << 1 ) | ( 0x1 << 0 ) );
                    dev->putReceiverRegValue( 0x05, wr_val );

                    do {
                        this_thread::sleep_for(chrono::microseconds(500));
                        dev->getReceiverRegValue( 0x05, rd_val[0] );
                        if ( rd_val[0] == 0xff ) {
                            cerr << "Critical error while registry reading. Is your device is broken?"
                                 << "Try do detach submodule and attach it again" << endl;
                            poller_running = false;
                            break;
                        }
                    } while ( ( rd_val[0] & 0x01 ) == 0x01 && poller_running );

                    auto cur_time = chrono::system_clock::now();
                    auto time_from_start = cur_time - start_time;
                    uint64_t ms_from_start = chrono::duration_cast<chrono::milliseconds>(time_from_start).count();

                    dev->getReceiverRegValue( 0x06, rd_val[1] );
                    dev->getReceiverRegValue( 0x07, rd_val[2] );
                    dev->getReceiverRegValue( 0x08, rd_val[3] );
                    dev->getReceiverRegValue( 0x09, rd_val[4] );
                    dev->getReceiverRegValue( 0x0A, rd_val[5] );

                    fprintf( flog, "%8" PRIu64 " ", ms_from_start);
                    for ( int i = 0; i < 6; i++ ) {
                        fprintf( flog, "%02X ", rd_val[i] );
                        rd_val[i] = 0x00;
                    }
                    fprintf( flog, "\n" );
                }
                //cerr << endl;
                //this_thread::sleep_for(chrono::milliseconds(20));
            }
            fclose(flog);
            cerr << "Poller thread finished" << endl;
        });

        for ( ;; ) {
            if ( bytes_to_dump ) {
                cerr << "\r";
            } else {
                cerr << endl << "Just testing. Press Ctrl-C to exit.  ";
            }
            if ( bytes_to_dump ) {
                DumperStatus_e status = dumper->GetStatus();
                if ( status == DS_DONE ) {
                    break;
                } else if ( status == DS_ERROR ) {
                    cerr << "Stop because of FILE IO errors" << endl;
                    break;
                }
                cerr << dumper->GetBytesToGo() / ( bytes_per_sample * CHIP_SR ) << " seconds to go. ";
            }

            info = dev->getDebugInfoFromBoard();
            info.overflows -= overs_cnt_at_start;
            cerr << "Overflows count: " << info.overflows << "    ";

            if ( info.overflows_inc ) {
                throw std::runtime_error( "### OVERFLOW DETECTED ON BOARD. DATA SKIP IS VERY POSSIBLE. EXITING ###" );
            }

            std::this_thread::sleep_for(chrono::milliseconds(iter_time_ms));
        }
        cerr << endl;

        cerr << "Dump done" << endl;
    } catch ( std::exception& e ){
        cerr << endl << "Error!" << endl;
        cerr << e.what();
    }

    dev->changeHandler(nullptr);
    delete dumper;

    poller_running = false;
    if ( poller.joinable() ) {
        poller.join();
    }

    delete dev;

    return 0;
}

