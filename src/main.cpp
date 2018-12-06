#include <boost/asio/io_service.hpp>
#include <boost/asio/ip/tcp.hpp>
/*#include <boost/asio/read_until.hpp>
#include <boost/asio/streambuf.hpp>
#include <boost/asio/write.hpp>
#include <boost/asio.hpp>*/
#include <boost/filesystem.hpp>
#include <boost/thread.hpp>
#include <fstream>

#include "server_http.hpp"

#include "json.hpp"
#include "client.hpp"

using boost::asio::ip::tcp;
using HttpServer = SimpleWeb::Server<SimpleWeb::HTTP>;

/*
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
}*/

int main(int argc, char* argv[]) {

	try {
//		if (argc != 3)
//		{
//			std::cerr << "Usage: Client <nodeIP> <meshPort>\n";
//			return 1;
//		}


		boost::asio::io_service io_service;
		tcp::resolver r(io_service);
		Client mesh(io_service);


		HttpServer server;
		server.config.port = 8080;

		// Default GET-example. If no other matches, this anonymous function will be called.
		// Will respond with content in the web/-directory, and its subdirectories.
		// Default file: index.html
		// Can for instance be used to retrieve an HTML 5 client that uses REST-resources on this server
		server.default_resource["GET"] = [](std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) {
			try {
				boost::filesystem::path p1{"web"};
				auto web_root_path = boost::filesystem::canonical(p1);
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
				//TODO change to 404
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

		server.resource["^/otaInfo"]["GET"] = [&](std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) {
			SimpleWeb::CaseInsensitiveMultimap header;
			header.emplace("Content-Type", "application/json");

			auto json = mesh.getOTAInfo();

			response->write(json.dump(), header);
		};

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

			    if((post["broadcast"].length() < 1 && post["nodeId"].length() < 1) || post["firmware"].length() < 1)
			    {
					response->write(SimpleWeb::StatusCode::client_error_bad_request, "POST DATA MISSING!");
					return;
			    }

				uint32_t nodeId = 0;
				bool broadcast = false;
				if (post["broadcast"].length() > 0) {
					broadcast = post["broadcast"] == "true";
					if (broadcast) {
						nodeId = 1;
					}
				} else {
					std::stringstream ss;
					ss << std::hex << post["nodeId"];
					ss >> nodeId;
				}

			    mesh.sendOTA(nodeId, post["firmware"], broadcast);

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
	} catch (std::exception& e) {
		std::cerr << "Exception: " << e.what() << "\n";
	}

	return 0;
}
