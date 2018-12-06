#include "client.hpp"


// Called by the user of the client class to initiate the connection process.
// The endpoint iterator will have been obtained using a tcp::resolver.
void Client::start(std::string dstIP, std::string dstPort) {
	if(!isConnected_) {
		tcp::resolver::iterator endpoint_iter = resolver_.resolve(tcp::resolver::query(dstIP, dstPort));
		startTime = std::chrono::high_resolution_clock::now();

		// Start the connect actor.
		start_connect(endpoint_iter);
	}
}

// This function terminates all the actors to shut down the connection. It
// may be called by the user of the Client class, or by the class itself in
// response to graceful termination or an unrecoverable error.
void Client::stop() {
	stopped_ = true;
	socket_.close();
	deadline_.cancel();
	nodeSync_timer_.cancel();
	isConnected_ = false;

}

void Client::start_connect(tcp::resolver::iterator endpoint_iter) {
	if (endpoint_iter != tcp::resolver::iterator()) {
		std::cout << "Trying " << endpoint_iter->endpoint() << "...\n";

		// Set a deadline for the connect operation.
//			deadline_.expires_from_now(boost::posix_time::seconds(60));

		// Start the asynchronous connect operation.
		socket_.async_connect(endpoint_iter->endpoint(),
				boost::bind(&Client::handle_connect, this, _1, endpoint_iter));
	} else {
		// There are no more endpoints to try. Shut down the client.
		stop();
	}
}

void Client::start_read() {
	// Set a deadline for the read operation.
//		deadline_.expires_from_now(boost::posix_time::seconds(30));

	// Start an asynchronous operation to read a newline-delimited message.
	boost::asio::async_read_until(socket_,
			input_buffer_,
			'\00',
			boost::bind(&Client::handle_read, this, _1));
}

void Client::handle_connect(const boost::system::error_code& ec, tcp::resolver::iterator endpoint_iter) {
	if (stopped_)
		return;

	// The async_connect() function automatically opens the socket at the start
	// of the asynchronous operation. If the socket is closed at this time then
	// the timeout handler must have run first.
	if (!socket_.is_open()) {
		std::cout << "Connect timed out\n";

		// Try the next available endpoint.
		start_connect(++endpoint_iter);
	}

	// Check if the connect operation failed before the deadline expired.
	else if (ec) {
		std::cout << "Connect error: " << ec.message() << "\n";

		// We need to close the socket used in the previous connection attempt
		// before starting a new one.
		socket_.close();

		// Try the next available endpoint.
		start_connect(++endpoint_iter);
	}

	// Otherwise we have successfully established a connection.
	else {
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
		boost::asio::async_write(socket_, boost::asio::buffer(jString),
				boost::bind(&Client::start_read, this));

	}
}

void Client::handle_read(const boost::system::error_code& ec) {
	if (stopped_)
		return;

	if (!ec) {
		std::string retVal((std::istreambuf_iterator<char>(&input_buffer_)), std::istreambuf_iterator<char>());
		json j = json::parse(retVal);

		uint32_t fromID = j["from"];

		std::cout << "From: ";
		std::cout << std::uppercase << std::hex << fromID;
		json jReply;
		std::string jString;

		switch((uint8_t)j["type"]) {
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
				jReply["version"] = "0";
                jReply["subs"] = json::array({});

                jString = jReply.dump();
                jString += '\0';
                boost::asio::async_write(
                        socket_,
                        boost::asio::buffer(jString),
                        boost::bind(&Client::start_read, this));

                break;

            case NODE_SYNC_REPLY:
                std::cout << std::left << std::setw(25) << " Type: NodeSyncReply" << " Subs: " <<  j["subs"] << std::endl;
                buildMeshTopology(j);
                break;

            case OTA:
			case OTA_BROADCAST:
                std::cout << std::left << std::setw(25) << " Type: OTA" << " Msg: " <<  j["msg"] << std::endl;
                sendOTA((uint8_t)j["type"] == OTA_BROADCAST, j);
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
	} else {
		std::cout << "Error on receive: " << ec.message() << "\n";

		stop();
	}
}












json Client::getTopology()
{
	auto mesh = json::object();
	mesh["nodes"] = meshNodes;
	mesh["edges"] = meshEdges;
	return mesh;
}

void Client::buildMeshTopology(json reply)
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
	nodeObj["version"] = reply["version"];
	nodeObj["group"] = "node";
	meshNodes.push_back(nodeObj);

	edgeObj["to"] = lastNode;
	meshEdges.push_back(edgeObj);

	meshNode(reply["subs"], lastNode);
}

void Client::meshNode(json node, uint32_t last)
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
		nodeObj["version"] = element["version"];
		nodeObj["group"] = "node";

		meshNodes.push_back(nodeObj);

		if(!element["subs"].empty())
		{
			meshNode(element["subs"], currNodeId);
		}
	}
}