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

#include <openssl/md5.h>

#include "meshDetect.hpp"


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


namespace neolib
{
	template<class Elem, class Traits>
	inline void hex_dump(const void* aData, std::size_t aLength, std::basic_ostream<Elem, Traits>& aStream, std::size_t aWidth = 16)
	{
		const char* const start = static_cast<const char*>(aData);
		const char* const end = start + aLength;
		const char* line = start;
		while (line != end)
		{
			aStream.width(4);
			aStream.fill('0');
			aStream << std::hex << line - start << " : ";
			std::size_t lineLength = std::min(aWidth, static_cast<std::size_t>(end - line));
			for (std::size_t pass = 1; pass <= 2; ++pass)
			{
				for (const char* next = line; next != end && next != line + aWidth; ++next)
				{
					char ch = *next;
					switch(pass)
					{
					case 1:
						aStream << (ch < 32 ? '.' : ch);
						break;
					case 2:
						if (next != line)
							aStream << " ";
						aStream.width(2);
						aStream.fill('0');
						aStream << std::hex << std::uppercase << static_cast<int>(static_cast<unsigned char>(ch));
						break;
					}
				}
				if (pass == 1 && lineLength != aWidth)
					aStream << std::string(aWidth - lineLength, ' ');
				aStream << " ";
			}
			aStream << std::endl;
			line = line + lineLength;
		}
	}
}


class client
{
public:
	client(boost::asio::io_service& io_service)
		: stopped_(false),
		  isConnected_(false),
		  socket_(io_service),
		  deadline_(io_service),
		  nodeSync_timer_(io_service),
		  upload_timer_(io_service),
		  resolver_(io_service),
		  file_size(0),
		  adjustTime(0)
	{
		meshNodes = json::array();
		meshEdges = json::array();
	}

	// Called by the user of the client class to initiate the connection process.
	// The endpoint iterator will have been obtained using a tcp::resolver.
	void start(std::string dstIP, std::string dstPort)
	{
		if(!isConnected_)
		{
			tcp::resolver::iterator endpoint_iter = resolver_.resolve(tcp::resolver::query(dstIP, dstPort));
			startTime = std::chrono::high_resolution_clock::now();

			// Start the connect actor.
			start_connect(endpoint_iter);
		}
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
		isConnected_ = false;

	}

	void sendOTA(uint32_t nodeId, std::string fwFile)
	{
		otaNodeId = nodeId;
		std::string path( fwFile );
		std::cout << "Open: " << path << std::endl;

		if(source_file)
		{
			source_file.close();
		}

        source_file.open(path, std::ios_base::binary|std::ios_base::ate);
        if(!source_file)
        {
            boost::mutex::scoped_lock lk(debug_mutex);
            std::cout << __LINE__ << "Failed to open " << path << std::endl;
            return;
        }
        otaFileSize = source_file.tellg();
        source_file.seekg(0);

        MD5_CTX mdContext;
        MD5_Init(&mdContext);
    	uint8_t otaMD5[MD5_DIGEST_LENGTH];

        while(source_file)
        {
        	source_file.read(buf.c_array(), (std::streamsize)buf.size());
        	MD5_Update(&mdContext, buf.c_array(), source_file.gcount());
        }

        MD5_Final(otaMD5,&mdContext);
        int i;
        std::cout << "Firmware MD5: ";
        std::stringstream ss;

        for(i = 0; i < MD5_DIGEST_LENGTH; i++)
        {
            ss << std::hex << (int)otaMD5[i];
        }

        std::string otaStr = ss.str();

        std::cout << otaStr << std::endl;

        source_file.clear();
        source_file.seekg(0, std::ios::beg);

        json j;
		j["from"] = 1;
		j["dest"] = otaNodeId;
		j["type"] = 7;
		j["msg"] = {{"type",0}, {"md5", otaStr}};

		std::string jString = j.dump();
		jString += '\0';

        boost::asio::async_write(socket_, boost::asio::buffer(jString), boost::bind(&client::handle_ota_write, this, boost::asio::placeholders::error));
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
			isConnected_ = true;
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
				std::cout << std::left << std::setw(25) << " Type: OTA" << " Msg: " <<  j["msg"] << std::endl;
				sendOTA();
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

	void handle_ota_write(const boost::system::error_code& error)
	{
		if (!error)
		{

		}
		else
		{
			std::cout << "ERROR handle_ota_write" << std::endl;
		}
	}

	void sendOTA()
	{
		if(source_file)
		{
			try {
			source_file.read(buf.c_array(), (std::streamsize)buf.size());
			if(source_file.gcount()<= 0)
			{
				boost::mutex::scoped_lock lk(debug_mutex);
				std::cout << "read file error" << std::endl;
				return;
			}
//			{
//				boost::mutex::scoped_lock lk(debug_mutex);
				std::cout << std::dec;
				std::cout << "Send " << source_file.gcount() << " bytes\t "<<  source_file.tellg() << "/" << otaFileSize << " bytes ("<< std::to_string( ((float) source_file.tellg() / otaFileSize)*100) <<")" << std::endl;
//			}
			std::string b64 = base64::encode(buf.c_array(), source_file.gcount());

			json j;
			j["from"] = 1;
			j["dest"] = otaNodeId;
			j["type"] = 7;
			j["msg"] = {{"type",1}, {"length", b64.length()}, {"data", b64}};

			std::string jString = j.dump();
			jString += '\0';

			async_write(socket_,
				boost::asio::buffer(jString),
				boost::bind(&client::handle_ota_write,
						this,//shared_from_this(),
						boost::asio::placeholders::error));
			}
			catch(const std::exception &e)
			{
				std::cout << e.what() << std::endl;
			}

		}
		else
		{
			std::cout << "OTA Finished!" << std::endl;
			source_file.close();

			json j;
			j["from"] = 1;
			j["dest"] = otaNodeId;
			j["type"] = 7;
			j["msg"] = {{"type",2}};

			std::string jString = j.dump();
			jString += '\0';

			boost::asio::async_write(socket_,
					boost::asio::buffer(jString),
					boost::bind(&client::handle_ota_write, this, boost::asio::placeholders::error));
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
	bool isConnected_;
	tcp::socket socket_;
	boost::asio::streambuf input_buffer_;
	deadline_timer deadline_;
	deadline_timer nodeSync_timer_;
	deadline_timer upload_timer_;
	tcp::resolver resolver_;

    boost::array<char, 512> buf;
    std::ifstream source_file;
    size_t file_size;
	uint32_t adjustTime;
	std::chrono::_V2::system_clock::time_point startTime;
	std::string inData;
	json meshNodes;
	json meshEdges;
	uint32_t otaNodeId = 0;
	uint32_t otaFileSize = 0;
	uint16_t nodeId = 0;
	uint16_t edgesId = 0;

};

int main(int argc, char* argv[])
{

	try
	{
//		if (argc != 3)
//		{
//			std::cerr << "Usage: client <nodeIP> <meshPort>\n";
//			return 1;
//		}


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


//		server.resource["/upload"]["POST"] = [](std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) {
//			std::string buffer;
//			buffer.resize(131072);
//			std::cout << "Getting File" << std::endl;
//
//			std::string boundary;
//			if(!getline(request->content, boundary)) {
//				response->write(SimpleWeb::StatusCode::client_error_bad_request);
//				return;
//			}
//
//			// go through all content parts
//			while(true) {
//				std::stringstream file; // std::stringstream is used as example output type
//				std::string filename;
//
//				auto header = SimpleWeb::HttpHeader::parse(request->content);
//				auto header_it = header.find("Content-Disposition");
//
//				if(header_it != header.end()) {
//					std::cout << "Header" << std::endl;
//					auto content_disposition_attributes = SimpleWeb::HttpHeader::FieldValue::SemicolonSeparatedAttributes::parse(header_it->second);
//
//					auto filename_it = content_disposition_attributes.find("filename");
//					if(filename_it != content_disposition_attributes.end()) {
//						filename = filename_it->second;
//						std::cout << "File " << filename << std::endl;
//						bool add_newline_next = false; // there is an extra newline before content boundary, this avoids adding this extra newline to file
//						// store file content in variable file
//					    MD5_CTX mdContext;
//					    MD5_Init(&mdContext);
//
//
//						while(true) {
//							request->content.getline(&buffer[0], static_cast<std::streamsize>(buffer.size()));
//							if(request->content.eof()) {
//								response->write(SimpleWeb::StatusCode::client_error_bad_request);
//								return;
//							}
//							auto size = request->content.gcount();
//
//							if(size >= 2 && (static_cast<size_t>(size - 1) == boundary.size() || static_cast<size_t>(size - 1) == boundary.size() + 2) && // last boundary ends with: --
//							std::strncmp(buffer.c_str(), boundary.c_str(), boundary.size() - 1 /*ignore \r*/) == 0 &&
//							buffer[static_cast<size_t>(size) - 2] == '\r') // buffer must also include \r at end
//								break;
//
//							if(add_newline_next) {
//								file.put('\n');
//								MD5_Update(&mdContext, "\n", 1);
//								add_newline_next = false;
//							}
//
//							if(!request->content.fail()) { // got line or section that ended with newline
//								file.write(buffer.c_str(), size - 1); // size includes newline character, but buffer does not
//								//neolib::hex_dump(buffer.c_str(), size-2, std::cout);
//								MD5_Update(&mdContext, buffer.c_str(), size - 1);
//								add_newline_next = true;
//							}
//							else
//							{
//								file.write(buffer.c_str(), size);
//								MD5_Update(&mdContext, buffer.c_str(), size);
//							}
//
//							request->content.clear(); // clear stream state
//						}
//
//						std::cout << "filename: " << filename << std::endl;
//
//
//					    unsigned char c[MD5_DIGEST_LENGTH];
//					    int i;
//
//					    MD5_Final(c,&mdContext);
//					    for(i = 0; i < MD5_DIGEST_LENGTH; i++) printf("%02x", c[i]);
//					}
//					else
//					{
//						auto filename_it = content_disposition_attributes.find("name");
//
//						std::cout << "No FILE" << std::endl;
//						bool add_newline_next = false; // there is an extra newline before content boundary, this avoids adding this extra newline to file
//						while(true) {
//							request->content.getline(&buffer[0], static_cast<std::streamsize>(buffer.size()));
//							if(request->content.eof()) {
//								response->write(SimpleWeb::StatusCode::client_error_bad_request);
//								return;
//							}
//							auto size = request->content.gcount();
//
//							if(size >= 2 && (static_cast<size_t>(size - 1) == boundary.size() || static_cast<size_t>(size - 1) == boundary.size() + 2) && // last boundary ends with: --
//							std::strncmp(buffer.c_str(), boundary.c_str(), boundary.size() - 1 /*ignore \r*/) == 0 &&
//							buffer[static_cast<size_t>(size) - 2] == '\r') // buffer must also include \r at end
//								break;
//
//							if(add_newline_next) {
//								file.put('\n');
//								add_newline_next = false;
//							}
//
//							if(!request->content.fail()) { // got line or section that ended with newline
//								file.write(buffer.c_str(), size - 1); // size includes newline character, but buffer does not
//								add_newline_next = true;
//							}
//							else
//								file.write(buffer.c_str(), size);
//
//							request->content.clear(); // clear stream state
//						}
//
//						std::cout << "Param: " << filename_it->second<< " content: " << std::endl
//							<< file.str() << std::endl;					}
//				}
//				else { // no more parts
//					response->write(); // Write empty success response
//					return;
//				}
//			}
//		};


		server.resource["^/netif$"]["GET"] = [](std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) {
			SimpleWeb::CaseInsensitiveMultimap header;
			header.emplace("Content-Type", "application/json");

			auto json = findMesh();

			response->write(json.dump(), header);
		};


		server.resource["^/mesh$"]["GET"] = [&](std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) {
			SimpleWeb::CaseInsensitiveMultimap header;
			header.emplace("Content-Type", "application/json");

			auto json = mesh.getTopology();

			response->write(json.dump(4), header);
		};


		server.resource["^/fw$"]["GET"] = [&](std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) {
			SimpleWeb::CaseInsensitiveMultimap header;
			header.emplace("Content-Type", "application/json");

			json j;

			boost::filesystem::path p("fw");
			boost::filesystem::directory_iterator end_itr;

			// cycle through the directory
			for(boost::filesystem::directory_iterator itr(p); itr != end_itr; ++itr)
			{
				// If it's not a directory, list it. If you want to list directories too, just remove this check.
				if (is_regular_file(itr->path())) {
					// assign current file name to current_file and echo it out to the console.
					std::string current_file = itr->path().string();
					std::string filename = itr->path().filename().string();
					if( filename[0] != '.')
					{
						j.push_back(current_file);
						std::cout << current_file << std::endl;
					}
				}
			}
			response->write(j.dump(4), header);
		};
		server.resource["^/connect$"]["POST"] = [&](std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) {
			SimpleWeb::CaseInsensitiveMultimap header;
			header.emplace("Content-Type", "application/json");

			std::cout << "POST connect" << std::endl;
		    auto fields = SimpleWeb::QueryString::parse(request->content.string());
		    std::string dstPort = "5555";

		    std::string dstIP = fields.find("dstIP")->second;

			if(fields.find("dstPort") != fields.end() && fields.find("dstPort")->second != "") {
				dstPort = fields.find("dstPort")->second;
			}
			mesh.start(dstIP, dstPort);

		    response->write("{}", header);

		};
		server.resource["^/json$"]["POST"] = [&](std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) {
			try {
			    auto fields = SimpleWeb::QueryString::parse(request->content.string());

			    std::map<std::string, std::string> post;
			    for(auto &field: fields)
			    {
			    	post[field.first] = field.second;
			        std::cout << field.first << ": " << field.second << "\n";
			    }

			    if(post["nodeId"].length() < 1 || post["firmware"].length() < 1)
			    {
					response->write(SimpleWeb::StatusCode::client_error_bad_request, "POST DATA MISSING!");
					return;
			    }

		        uint32_t nodeId;
		        std::stringstream ss;
		        ss << std::hex << post["nodeId"];
		        ss >> nodeId;

			    mesh.sendOTA(nodeId, post["firmware"]);

			    response->write(SimpleWeb::StatusCode::success_ok, "Node: " + post["nodeId"] + " fw: " + post["firmware"]);
			}
			catch(const std::exception &e) {
				*response << "HTTP/1.1 400 Bad Request\r\nContent-Length: " << strlen(e.what()) << "\r\n\r\n" << e.what();
			}

		};

		boost::thread webServer_thread(boost::bind(&HttpServer::start, &server));
		std::cout << "Webserver running on localhost:"  << server.config.port << std::endl;

	    auto work = boost::make_shared<boost::asio::io_service::work>(io_service); // Dummy load to keep io_service alive
		io_service.run();
		std::cout << "IOS End"<<std::endl;
		webServer_thread.join();
	}
	catch (std::exception& e)
	{
		std::cerr << "Exception: " << e.what() << "\n";
	}

	return 0;
}
