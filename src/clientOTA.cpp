#include "client.hpp"

std::string getHex(uint32_t id) {
	std::stringstream stream;
	stream << std::uppercase << std::hex << id;
	return std::string(stream.str());
}

json Client::getOTAInfo() {
	auto mesh = json::object();
	if (runningOta_) {
		if (otaNodeId == 1) {
			mesh["type"] = 1;
		} else {
			mesh["type"] = 0;
			mesh["id"] = getHex(otaNodeId);
		}
		mesh["progress"] = otaPercentageDone;
		if (otaErroredIds.size() > 0) {
			mesh["node_error"] = otaErroredIds;
			otaErroredIds.clear();
		}
	} else {
		mesh["error"] = "No OTA update at the moment";
	}
	return mesh;
}

void Client::handle_ota_write(const boost::system::error_code& error) {
	if (error) {
		std::cout << "ERROR handle_ota_write" << std::endl;
	}
}

void Client::sendOTA(uint32_t nodeId, std::string fwFile, bool broadcast) {
	otaNodeId = nodeId;
	std::string path( fwFile );
	std::cout << "Open: " << path << std::endl;

	if(source_file) {
		source_file.close();
	}

	source_file.open(path, std::ios_base::binary|std::ios_base::ate);
	if(!source_file) {
		boost::mutex::scoped_lock lk(debug_mutex);
		std::cout << __LINE__ << "Failed to open " << path << std::endl;
		return;
	}
	otaFileSize = source_file.tellg();
	source_file.seekg(0);

	MD5_CTX mdContext;
	MD5_Init(&mdContext);
	uint8_t otaMD5[MD5_DIGEST_LENGTH];

	while(source_file) {
		source_file.read(buf.c_array(), (std::streamsize)buf.size());
		MD5_Update(&mdContext, buf.c_array(), source_file.gcount());
	}

	MD5_Final(otaMD5,&mdContext);
	int i;
	std::cout << "Firmware MD5: ";
	std::stringstream ss;

	for(i = 0; i < MD5_DIGEST_LENGTH; i++) {
		ss << std::hex << (int)otaMD5[i];
	}

	std::string otaStr = ss.str();

	std::cout << otaStr << std::endl;

	source_file.clear();
	source_file.seekg(0, std::ios::beg);

	json j;
	j["from"] = 1;
	j["dest"] = otaNodeId;
	j["type"] = broadcast ? OTA_BROADCAST: OTA;
	j["msg"] = {{"type",0}, {"md5", otaStr}};

	std::string jString = j.dump();
	jString += '\0';
	runningOta_ = true;

	boost::asio::async_write(socket_, boost::asio::buffer(jString), boost::bind(&Client::handle_ota_write, this, boost::asio::placeholders::error));
}

void Client::sendOTA(bool broadcast, json val) {
	bool error = val["msg"] == "Error";
	if (error) {
		otaErroredIds.push_back(getHex((uint32_t)val["from"]));

		if (!broadcast) {
			std::cout << "OTA Aborted, device errored!" << std::endl;
			source_file.close();

			json j;
            j["from"] = 1;
            j["dest"] = otaNodeId;
            j["type"] = broadcast ? OTA_BROADCAST: OTA;
            j["msg"] = {{"type",3}};

			std::string jString = j.dump();
            jString += '\0';
			async_write(socket_,
                boost::asio::buffer(jString),
                boost::bind(&Client::handle_ota_write, this, boost::asio::placeholders::error));
			return;
		}
	}
	if(source_file) {
		try {
            source_file.read(buf.c_array(), (std::streamsize)buf.size());
            if(source_file.gcount()<= 0) {
                boost::mutex::scoped_lock lk(debug_mutex);
                std::cout << "read file error" << std::endl;
                return;
            }
            std::cout << std::dec;
            float f = ((float) source_file.tellg() / otaFileSize) * 100;
            if (f < 0) f = 100;
            otaPercentageDone = std::to_string(f);
            std::cout << "Send " << source_file.gcount() << " bytes\t "<<  source_file.tellg() << "/" << otaFileSize << " bytes ("<< otaPercentageDone <<")" << std::endl;

            std::string b64 = base64::encode(buf.c_array(), source_file.gcount());

            json j;
            j["from"] = 1;
            j["dest"] = otaNodeId;
            j["type"] = broadcast ? OTA_BROADCAST: OTA;
            j["msg"] = {{"type",1}, {"length", b64.length()}, {"data", b64}};

            std::string jString = j.dump();
            jString += '\0';

            async_write(socket_,
                boost::asio::buffer(jString),
                boost::bind(&Client::handle_ota_write,
                        this,//shared_from_this(),
                        boost::asio::placeholders::error));
		} catch(const std::exception &e) {
			std::cout << e.what() << std::endl;
		}

	} else {
		std::cout << "OTA Finished!" << std::endl;
		source_file.close();

		json j;
		j["from"] = 1;
		j["dest"] = otaNodeId;
		j["type"] = broadcast ? OTA_BROADCAST: OTA;
		j["msg"] = {{"type",2}};

		std::string jString = j.dump();
		jString += '\0';
		runningOta_ = false;
		otaNodeId = 0;
		otaErroredIds.clear();
		otaPercentageDone = "0";

		boost::asio::async_write(socket_,
				boost::asio::buffer(jString),
				boost::bind(&Client::handle_ota_write, this, boost::asio::placeholders::error));
	}
}