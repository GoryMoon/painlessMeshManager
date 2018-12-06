#include "client.hpp"

uint32_t Client::nodeTime() {
	auto elapsed = std::chrono::high_resolution_clock::now() - startTime;

	uint32_t microseconds = std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();
	return microseconds + adjustTime;
}

void Client::handleTimeSync(json &j) {
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
		boost::asio::async_write(
				socket_,
				boost::asio::buffer(jString),
				boost::bind(&Client::start_read, this));

	} else {
		tS.t0 = j["msg"]["t0"];
		tS.t1 = j["msg"]["t1"];
		tS.t2 = j["msg"]["t2"];

		uint32_t offset = ((int32_t)(tS.t1 - tS.t0) / 2) + ((int32_t)(tS.t2 - timeReceived) / 2);
		adjustTime += offset;

		std::cout << "TimeOffset: " << std::to_string((int32_t)offset) << " New Time: " << std::to_string(nodeTime()) << std::endl;
	}

}

void Client::nodeSyncTimerArm() {
//		nodeSync_timer_.expires_from_now(boost::posix_time::seconds(10));
//		nodeSync_timer_.async_wait(boost::bind(&Client::nodeSyncTask, this));
}

void Client::nodeSyncTask() {
	std::cout << "Send NodeSyncRequest" << std::endl;
	json jReply;
	jReply["from"] = 1;
	jReply["dest"] = 0;
	jReply["type"] = NODE_SYNC_REQUEST;
	jReply["subs"] = json::array({});

	std::string jString = jReply.dump();
	jString += '\0';

	boost::asio::async_write(
			socket_,
			boost::asio::buffer(jString),
			boost::bind(&Client::nodeSyncTimerArm, this));

}