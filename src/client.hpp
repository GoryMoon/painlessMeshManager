#include <boost/asio/ip/tcp.hpp>
#include <boost/array.hpp>
#include <boost/bind.hpp>
#include <boost/thread.hpp>
#include <iostream>
#include <fstream>

#include <chrono>
#include <openssl/md5.h>

#include "base64_rfc4648.hpp"
#include "server_http.hpp"
#include "json.hpp"
#include "meshDetect.hpp"

using boost::asio::deadline_timer;
using boost::asio::ip::tcp;
using json = nlohmann::json;
using base64 = cppcodec::base64_rfc4648;
using time_stamp = std::chrono::time_point<std::chrono::system_clock, std::chrono::microseconds>;
using namespace std::chrono;

struct timeSync {
    uint32_t type;
    uint32_t t0;
    uint32_t t1;
    uint32_t t2;
};

enum packageType {
    DROP                    = 3,
    TIME_SYNC               = 4,
    NODE_SYNC_REQUEST       = 5,
    NODE_SYNC_REPLY         = 6,
    OTA				        = 7,
    BROADCAST               = 8,  //application data for everyone
    SINGLE                  = 9,  //application data for a single node
    OTA_BROADCAST           = 10  //used to update all connected devices
};

static boost::mutex debug_mutex;

class Client {
private:
    void start_read();
    void start_connect(tcp::resolver::iterator endpoint_iter);
    void handle_connect(const boost::system::error_code& ec, tcp::resolver::iterator endpoint_iter);
    void handle_read(const boost::system::error_code& ec);

    uint32_t nodeTime();
    void handleTimeSync(json &j);
    void nodeSyncTimerArm();
    void nodeSyncTask();

    void handle_ota_write(const boost::system::error_code& error);
    void sendOTA(bool broadcast, json val);
    
    void buildMeshTopology(json reply);
    void meshNode(json node, uint32_t last);


	bool stopped_;
	bool isConnected_;
    bool runningOta_;

	tcp::socket socket_;
	boost::asio::streambuf input_buffer_;
	deadline_timer deadline_;
	deadline_timer nodeSync_timer_;
	deadline_timer upload_timer_;
	tcp::resolver resolver_;

    boost::array<char, 512> buf;
    std::ifstream source_file;
	uint32_t adjustTime;
	std::chrono::high_resolution_clock::time_point startTime;
	std::string inData;
	json meshNodes;
	json meshEdges;
	uint32_t otaNodeId = 0;
	uint32_t otaFileSize = 0;
	uint16_t nodeId = 0;
	uint16_t edgesId = 0;

	std::string otaPercentageDone;
    std::list<std::string> otaErroredIds;

public:
    Client(boost::asio::io_service& io_service)
	: stopped_(false),
		isConnected_(false),
        runningOta_(false),
		socket_(io_service),
		deadline_(io_service),
		nodeSync_timer_(io_service),
		upload_timer_(io_service),
		resolver_(io_service),
		adjustTime(0)
    {
        meshNodes = json::array();
        meshEdges = json::array();
    }
    void start(std::string dstIP, std::string dstPort);
    void stop();

    json getTopology();

    json getOTAInfo();
    void sendOTA(uint32_t nodeId, std::string fwFile, bool broadcast);
};