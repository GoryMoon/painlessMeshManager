#include <boost/asio/deadline_timer.hpp>
#include <boost/asio/io_service.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/read_until.hpp>
#include <boost/asio/streambuf.hpp>
#include <boost/asio/write.hpp>
#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/thread.hpp>
#include <boost/array.hpp>
#include <iostream>
#include <fstream>

#include <chrono>

#include "cppcodec/cppcodec/base64_rfc4648.hpp"
#include "json.hpp"

#include <boost/filesystem.hpp>
#include "swServer/server_http.hpp"

using boost::asio::deadline_timer;
using boost::asio::ip::tcp;
using json = nlohmann::json;
using base64 = cppcodec::base64_rfc4648;

static boost::mutex debug_mutex;

enum packageType {
    DROP                    = 3,
    TIME_SYNC               = 4,
    NODE_SYNC_REQUEST       = 5,
    NODE_SYNC_REPLY         = 6,
    OTA				        = 7,
    BROADCAST               = 8,  //application data for everyone
    SINGLE                  = 9   //application data for a single node
};

struct timeSync {
    uint32_t type;
    uint32_t t0;
    uint32_t t1;
    uint32_t t2;
};

using namespace std::chrono;
using time_stamp = std::chrono::time_point<std::chrono::system_clock, std::chrono::microseconds>;

using HttpServer = SimpleWeb::Server<SimpleWeb::HTTP>;


class client
{
public:
	client(boost::asio::io_service& io_service)
		: stopped_(false),
			socket_(io_service),
			deadline_(io_service),
			nodeSync_timer_(io_service),
			upload_timer_(io_service),
			file_size(0),
			adjustTime(0)
	{
		meshNodes = json::array();
		meshEdges = json::array();
	}

	// Called by the user of the client class to initiate the connection process.
	// The endpoint iterator will have been obtained using a tcp::resolver.
	void start(tcp::resolver::iterator endpoint_iter)
	{
		startTime = std::chrono::high_resolution_clock::now();

		// Start the connect actor.
		start_connect(endpoint_iter);
	}

	json getTopology()
	{
		auto mesh = json::object();
		mesh["nodes"] = meshNodes;
		mesh["edges"] = meshEdges;
		return mesh;
	}


	// This function terminates all the actors to shut down the connection. It
	// may be called by the user of the client class, or by the class itself in
	// response to graceful termination or an unrecoverable error.
	void stop()
	{
		stopped_ = true;
		socket_.close();
		deadline_.cancel();
		nodeSync_timer_.cancel();
	}

private:
	void start_connect(tcp::resolver::iterator endpoint_iter)
	{
		if (endpoint_iter != tcp::resolver::iterator())
		{
			std::cout << "Trying " << endpoint_iter->endpoint() << "...\n";

			// Set a deadline for the connect operation.
//			deadline_.expires_from_now(boost::posix_time::seconds(60));

			// Start the asynchronous connect operation.
			socket_.async_connect(endpoint_iter->endpoint(),
					boost::bind(&client::handle_connect, this, _1, endpoint_iter));
		}
		else
		{
			// There are no more endpoints to try. Shut down the client.
			stop();
		}
	}

	void handle_connect(const boost::system::error_code& ec, tcp::resolver::iterator endpoint_iter)
	{
		if (stopped_)
			return;

		// The async_connect() function automatically opens the socket at the start
		// of the asynchronous operation. If the socket is closed at this time then
		// the timeout handler must have run first.
		if (!socket_.is_open())
		{
			std::cout << "Connect timed out\n";

			// Try the next available endpoint.
			start_connect(++endpoint_iter);
		}

		// Check if the connect operation failed before the deadline expired.
		else if (ec)
		{
			std::cout << "Connect error: " << ec.message() << "\n";

			// We need to close the socket used in the previous connection attempt
			// before starting a new one.
			socket_.close();

			// Try the next available endpoint.
			start_connect(++endpoint_iter);
		}

		// Otherwise we have successfully established a connection.
		else
		{
			std::cout << "Connected to " << endpoint_iter->endpoint() << "\n";
//			nodeSyncTimerArm();
			nodeSyncTask();

			json jReply;
			jReply["from"] = 1;
			jReply["dest"] = 0;
			jReply["type"] = NODE_SYNC_REPLY;
			jReply["subs"] = json::array({});

			std::string jString = jReply.dump();
			jString += '\0';
//			std::cout << jString << std::endl;
			boost::asio::async_write(socket_, boost::asio::buffer(jString),
					boost::bind(&client::start_read, this));

		}
	}

	void start_read()
	{
		// Set a deadline for the read operation.
//		deadline_.expires_from_now(boost::posix_time::seconds(30));

		// Start an asynchronous operation to read a newline-delimited message.
		boost::asio::async_read_until(socket_,
				input_buffer_,
				'\00',
				boost::bind(&client::handle_read, this, _1));
	}

	uint32_t nodeTime()
	{
		auto elapsed = std::chrono::high_resolution_clock::now() - startTime;

		uint32_t microseconds = std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();
		return microseconds + adjustTime;
	}

	void handle_read(const boost::system::error_code& ec)
	{
		if (stopped_)
			return;

		if (!ec)
		{
			std::string retVal((std::istreambuf_iterator<char>(&input_buffer_)), std::istreambuf_iterator<char>());
			json j = json::parse(retVal);

			uint32_t fromID = j["from"];

			std::cout << "From: ";
			std::cout << std::uppercase << std::hex << fromID;
			json jReply;
			std::string jString;

			switch((uint8_t)j["type"])
			{
			case TIME_SYNC:
				std::cout << std::left << std::setw(25) << " Type: TimeSync" << " Msg: " <<  j["msg"] << std::endl;

				handleTimeSync(j);

				break;

			case NODE_SYNC_REQUEST:
				std::cout << std::left << std::setw(25) << " Type: NodeSyncRequest" << " Subs: " <<  j["subs"] <<std::endl;
				buildMeshTopology(j);

				jReply["from"] = 1;
				jReply["dest"] = j["from"];
				jReply["type"] = 6;
				jReply["subs"] = json::array({});

				jString = jReply.dump();
				jString += '\0';
//				std::cout << jString << std::endl;
				boost::asio::async_write(
						socket_,
						boost::asio::buffer(jString),
						boost::bind(&client::start_read, this));

				break;

			case NODE_SYNC_REPLY:
				std::cout << std::left << std::setw(25) << " Type: NodeSyncReply" << " Subs: " <<  j["subs"] << std::endl;
				buildMeshTopology(j);
				break;

			case OTA:
				break;

			case BROADCAST:
				std::cout << std::left << std::setw(25) << " Type: Broadcast" << " Msg: " <<  j["msg"] << std::endl;
				break;

			case SINGLE:
				std::cout << std::left << std::setw(25) << " Type: Single" << " Msg: " <<  j["msg"] << std::endl;
				break;

			default:
				std::cout << std::left << std::setw(25) << " Type: Unknown" << j["type"] << " Msg: " <<  j["msg"] << std::endl;

			}

			start_read();
		}
		else
		{
			std::cout << "Error on receive: " << ec.message() << "\n";

			stop();
		}
	}

	void handleTimeSync(json &j)
	{
		uint32_t timeReceived = nodeTime();
        timeSync tS;

        tS.type = j["msg"]["type"];
        if (tS.type != 2) {
			json pack;
			pack["dest"] = j["from"];
			pack["from"] = 1;
			pack["type"] = TIME_SYNC;
			if (tS.type == 0) {
				tS.t0 = nodeTime();
				pack["msg"]["t0"] = tS.t0;
			} else if (tS.type == 1) {
				tS.t1 = timeReceived;
				tS.t2 = nodeTime();
				pack["msg"]["t1"] = tS.t1;
				pack["msg"]["t2"] = tS.t2;
			}
			++tS.type;

			pack["msg"]["type"] = tS.type;

			std::string jString = pack.dump();
			jString += '\0';
//			std::cout << jString << std::endl;
			boost::asio::async_write(
					socket_,
					boost::asio::buffer(jString),
					boost::bind(&client::start_read, this));

		} else {
			tS.t0 = j["msg"]["t0"];
			tS.t1 = j["msg"]["t1"];
			tS.t2 = j["msg"]["t2"];

			uint32_t offset = ((int32_t)(tS.t1 - tS.t0) / 2) + ((int32_t)(tS.t2 - timeReceived) / 2);
			adjustTime += offset;

			std::cout << "TimeOffset: " << std::to_string((int32_t)offset) << " New Time: " << std::to_string(nodeTime()) << std::endl;

		}

	}

//	void start_write()
//	{
//		if (stopped_)
//			return;
//
//		std::cout << "Send..." << std::endl;
//
//		json j;
//		j["from"] = 1;
//		j["dest"] = 2140347777;
//		j["type"] = 8;
//		j["msg"] = {{"topic", "logServer"}};
//
//
//		std::string jString = j.dump();
//		jString += '\0';
//		std::cout << jString << std::endl;
//		boost::asio::async_write(
//				socket_,
//				boost::asio::buffer(jString),
//				boost::bind(&client::handle_write, this, _1));
//
//	}
//
//	void handle_write(const boost::system::error_code& ec)
//	{
//		if (stopped_)
//			return;
//
//		if (!ec)
//		{
//		}
//		else
//		{
//			std::cout << "Error on heartbeat: " << ec.message() << "\n";
//
//			stop();
//		}
//	}

	void sendFile()
	{
		std::string path( "firmware.bin");
		std::cout << "Open: " << path << std::endl;
        source_file.open(path, std::ios_base::binary|std::ios_base::ate);
        if(!source_file)
        {
            boost::mutex::scoped_lock lk(debug_mutex);
            std::cout << __LINE__ << "Failed to open " << path << std::endl;
            return;
        }
        file_size = source_file.tellg();
        source_file.seekg(0);

  		json j;
		j["from"] = 1;
		j["dest"] = 2140347777;
		j["type"] = 7;
		j["msg"] = {{"type",0}, {"size", file_size}};

		std::string jString = j.dump();
		jString += '\0';

        boost::asio::async_write(socket_, boost::asio::buffer(jString), boost::bind(&client::handle_downstream_write, this, boost::asio::placeholders::error));

	}

	void handle_downstream_write(const boost::system::error_code& error)
	{
		if (!error)
		{
			boost::asio::async_read_until(socket_,
					input_buffer_,
					'\00',
					boost::bind(&client::handle_downstream_read,
							this,
							boost::asio::placeholders::error,
							boost::asio::placeholders::bytes_transferred));
		}
		else
		{
			std::cout << "ERROR handle_downstream_write" << std::endl;
		}
	}

	void handle_downstream_read(const boost::system::error_code& error, const size_t& bytes_transferred)
	{
		if (!error)
		{
			std::string retVal((std::istreambuf_iterator<char>(&input_buffer_)), std::istreambuf_iterator<char>());
			json j = json::parse(retVal);

			if(j["type"] == 7)
			{
				if(source_file)
				{
					source_file.read(buf.c_array(), (std::streamsize)buf.size());
					if(source_file.gcount()<= 0)
					{
						boost::mutex::scoped_lock lk(debug_mutex);
						std::cout << "read file error" << std::endl;
						return;
					};
					{
						boost::mutex::scoped_lock lk(debug_mutex);
						std::cout << "Send " << source_file.gcount() << "bytes, total: " << source_file.tellg() << " bytes.\r";
					}
					std::string b64 = base64::encode(buf.c_array(), source_file.gcount());

					json j;
					j["from"] = 1;
					j["dest"] = 2140347777;
					j["type"] = 7;
					j["msg"] = {{"type",1}, {"length", b64.length()}, {"data", b64}};

					std::string jString = j.dump();
					jString += '\0';

					async_write(socket_,
						boost::asio::buffer(jString),
						boost::bind(&client::handle_downstream_write,
								this,//shared_from_this(),
								boost::asio::placeholders::error));

				}
				else
				{
					std::cout << "File Error" << std::endl;
					json j;
					j["from"] = 1;
					j["dest"] = 2140347777;
					j["type"] = 7;
					j["msg"] = {{"type",2}};

					std::string jString = j.dump();
					jString += '\0';
//					std::cout << jString << std::endl;

					boost::asio::async_write(socket_,
							boost::asio::buffer(jString),
							boost::bind(&client::handle_downstream_write, this, boost::asio::placeholders::error));
				}
			}
			else
			{
				handle_downstream_write(make_error_code(boost::system::errc::success));
			}
		}
		else
		{
			std::cout << "ERROR handle_downstream_read" << std::endl;
		}
	}

	void nodeSyncTimerArm()
	{
//		nodeSync_timer_.expires_from_now(boost::posix_time::seconds(10));
//		nodeSync_timer_.async_wait(boost::bind(&client::nodeSyncTask, this));
	}
	void nodeSyncTask()
	{
		std::cout << "Send NodeSyncRequest" << std::endl;
		json jReply;
		jReply["from"] = 1;
		jReply["dest"] = 0;
		jReply["type"] = NODE_SYNC_REQUEST;
		jReply["subs"] = json::array({});

		std::string jString = jReply.dump();
		jString += '\0';
//		std::cout << jString << std::endl;
		boost::asio::async_write(
				socket_,
				boost::asio::buffer(jString),
				boost::bind(&client::nodeSyncTimerArm, this));

	}


	void buildMeshTopology(json reply)
	{
		meshNodes.clear();
		meshEdges.clear();
		nodeId = 0;
		edgesId = 0;

		auto nodeObj = json::object();
		auto edgeObj = json::object();

		uint32_t lastNode = nodeId++;

		edgeObj["id"] = ++edgesId;

		nodeObj["id"] = lastNode;
		nodeObj["label"] = "PC";
		nodeObj["color"] = "#FFA000";
		nodeObj["group"] = "pc";
		meshNodes.push_back(nodeObj);

		edgeObj["from"] = lastNode;

		lastNode = nodeId++;
		nodeObj.clear();
		nodeObj["id"] = lastNode;
		std::stringstream stream;
		stream << std::uppercase <<std::hex << (uint32_t)reply["from"];

		nodeObj["label"] = std::string(stream.str());
		nodeObj["group"] = "node";
		meshNodes.push_back(nodeObj);

		edgeObj["to"] = lastNode;
		meshEdges.push_back(edgeObj);

		meshNode(reply["subs"], lastNode);
	}

	void meshNode(json node, uint32_t last)
	{

		for(auto& element : node) {
			uint32_t nId = element["nodeId"];
			uint32_t currNodeId = nodeId++;
			auto edgeObj = json::object();
			edgeObj["id"] = ++edgesId;
			edgeObj["from"] = last;
			edgeObj["to"] = currNodeId;

			meshEdges.push_back(edgeObj);

			auto nodeObj = json::object();
			nodeObj["id"] = currNodeId;
			std::stringstream stream;
			stream << std::uppercase << std::hex << nId;

			nodeObj["label"] = std::string(stream.str());
			nodeObj["group"] = "node";

			meshNodes.push_back(nodeObj);

			if(!element["subs"].empty())
			{
				meshNode(element["subs"], currNodeId);
			}
		}
	}

private:
	bool stopped_;
	tcp::socket socket_;
	boost::asio::streambuf input_buffer_;
	deadline_timer deadline_;
	deadline_timer nodeSync_timer_;
	deadline_timer upload_timer_;
    boost::array<char, 512> buf;
    std::ifstream source_file;
    size_t file_size;
	uint32_t adjustTime;
	std::chrono::_V2::system_clock::time_point startTime;
	std::string inData;
	json meshNodes;
	json meshEdges;
	uint16_t nodeId = 0;
	uint16_t edgesId = 0;
};

int main(int argc, char* argv[])
{
	try
	{
		if (argc != 3)
		{
			std::cerr << "Usage: client <nodeIP> <meshPort>\n";
			return 1;
		}


		boost::asio::io_service io_service;
		tcp::resolver r(io_service);
		client mesh(io_service);


		HttpServer server;
		server.config.port = 8080;

		// Default GET-example. If no other matches, this anonymous function will be called.
		// Will respond with content in the web/-directory, and its subdirectories.
		// Default file: index.html
		// Can for instance be used to retrieve an HTML 5 client that uses REST-resources on this server
		server.default_resource["GET"] = [](std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) {
			try {
				auto web_root_path = boost::filesystem::canonical("web");
				auto path = boost::filesystem::canonical(web_root_path / request->path);
				// Check if path is within web_root_path
				if(std::distance(web_root_path.begin(), web_root_path.end()) > std::distance(path.begin(), path.end()) ||
				!std::equal(web_root_path.begin(), web_root_path.end(), path.begin()))
					throw std::invalid_argument("path must be within root path");

				if(boost::filesystem::is_directory(path))
					path /= "index.html";

				SimpleWeb::CaseInsensitiveMultimap header;

				// Uncomment the following line to enable Cache-Control
				// header.emplace("Cache-Control", "max-age=86400");

				auto ifs = std::make_shared<std::ifstream>();
				ifs->open(path.string(), std::ifstream::in | std::ios::binary | std::ios::ate);

				if(*ifs) {
					auto length = ifs->tellg();
					ifs->seekg(0, std::ios::beg);

					header.emplace("Content-Length", to_string(length));
					response->write(header);

					// Trick to define a recursive function within this scope (for example purposes)
					class FileServer {
					public:
						static void read_and_send(const std::shared_ptr<HttpServer::Response> &response, const std::shared_ptr<std::ifstream> &ifs) {
							// Read and send 128 KB at a time
							static std::vector<char> buffer(131072); // Safe when server is running on one thread
							std::streamsize read_length;
							if((read_length = ifs->read(&buffer[0], static_cast<std::streamsize>(buffer.size())).gcount()) > 0) {
								response->write(&buffer[0], read_length);
								if(read_length == static_cast<std::streamsize>(buffer.size())) {
									response->send([response, ifs](const SimpleWeb::error_code &ec) {
										if(!ec)
											read_and_send(response, ifs);
										else
											std::cerr << "Connection interrupted" << std::endl;
									});
								}
							}
						}
					};
					FileServer::read_and_send(response, ifs);
				}
				else
					throw std::invalid_argument("could not read file");
			}
			catch(const std::exception &e) {
				response->write(SimpleWeb::StatusCode::client_error_bad_request, "Could not open path " + request->path + ": " + e.what());
			}
		};


//		server.resource["^/netif$"]["GET"] = [](std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) {
//			SimpleWeb::CaseInsensitiveMultimap header;
//			header.emplace("Content-Type", "application/json");
//
//			auto meshJ = std::make_shared<json>();
//			findMesh(meshJ);
//
//			response->write((*meshJ).dump(), header);
//		};


		server.resource["^/mesh$"]["GET"] = [&](std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) {
			SimpleWeb::CaseInsensitiveMultimap header;
			header.emplace("Content-Type", "application/json");

			auto json = mesh.getTopology();

			response->write(json.dump(4), header);
		};

		boost::thread webServer_thread(boost::bind(&HttpServer::start, &server));

		mesh.start(r.resolve(tcp::resolver::query(argv[1], argv[2])));

		io_service.run();
		webServer_thread.join();
	}
	catch (std::exception& e)
	{
		std::cerr << "Exception: " << e.what() << "\n";
	}

	return 0;
}
