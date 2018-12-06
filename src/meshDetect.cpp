
#if defined(_WIN32) || defined(_WIN64)
	#include <winsock2.h>
	#include <iphlpapi.h>
	#include <boost/format.hpp>
	#define MALLOC(x) HeapAlloc(GetProcessHeap(), 0, (x))
	#define FREE(x) HeapFree(GetProcessHeap(), 0, (x))
#else /* Non Windows */
	#define _GNU_SOURCE
	#include <netdb.h>
	#include <sys/types.h>
	#include <ifaddrs.h>
	#include <arpa/inet.h>
	#include <string.h>
	#include <sys/socket.h>
	#include <sys/ioctl.h>
	#include <net/if.h>
	#include <map>
	#ifdef __linux__
		#include <netpacket/packet.h>
		#include <net/ethernet.h>
	#else
		#include <net/if_dl.h>
	#endif
#endif /* Non Windows */
#include <stdlib.h>
#include <stdio.h>
#include <iostream>

#include "json.hpp"

using json = nlohmann::json;
using namespace std; 

int findLastIndex(char* str, char x) 
{ 
    int index = -1; 
    for (size_t i = 0; i < strlen(str); i++) 
        if (str[i] == x) 
            index = i; 
    return index; 
}

char* substring(const char* str, size_t begin, size_t len) 
{ 
  if (str == 0 || strlen(str) == 0 || strlen(str) < begin || strlen(str) < (begin+len)) 
    return 0; 

  return strndup(str + begin, len); 
} 

json findMesh()
{

    /* Declare and initialize variables */

	// It is possible for an adapter to have multiple
	// IPv4 addresses, gateways, and secondary WINS servers
	// assigned to the adapter.
	//
	// Note that this sample code only prints out the
	// first entry for the IP address/mask, and gateway, and
	// the primary and secondary WINS server for each adapter.
	json adapters;
#if defined(_WIN32) || defined(_WIN64)
    PIP_ADAPTER_INFO pAdapterInfo;
    PIP_ADAPTER_INFO pAdapter = NULL;
    DWORD dwRetVal = 0;
    UINT i;

    ULONG ulOutBufLen = sizeof(IP_ADAPTER_INFO);
    pAdapterInfo = (IP_ADAPTER_INFO *) MALLOC(sizeof(IP_ADAPTER_INFO));
    if (pAdapterInfo == NULL) {
        printf("Error allocating memory needed to call GetAdaptersinfo\n");
        return -1;
		
	}
	// Make an initial call to GetAdaptersInfo to get
	// the necessary size into the ulOutBufLen variable
    if (GetAdaptersInfo(pAdapterInfo, &ulOutBufLen) == ERROR_BUFFER_OVERFLOW) {
        FREE(pAdapterInfo);
        pAdapterInfo = (IP_ADAPTER_INFO *) MALLOC(ulOutBufLen);
        if (pAdapterInfo == NULL) {
            printf("Error allocating memory needed to call GetAdaptersinfo\n");
            return -1;
        }
    }

    if ((dwRetVal = GetAdaptersInfo(pAdapterInfo, &ulOutBufLen)) == NO_ERROR) {
        pAdapter = pAdapterInfo;

		while (pAdapter) {
        	auto adapterInfo = json::object();
        	adapterInfo["Name"] = pAdapter->Description;

			switch (pAdapter->Type) {
				case 71:
					adapterInfo["Type"] = "WIFI";
					break;

				default:
					pAdapter = pAdapter->Next;
					continue;
					break;
			}

			boost::format MACaddr("%02X-%02X-%02X-%02X-%02X-%02X");

			for (i = 0; i < pAdapter->AddressLength; i++) {
				MACaddr % static_cast<unsigned int>(pAdapter->Address[i]);
			}

			adapterInfo["Mac"] = MACaddr.str();
			adapterInfo["Ip"] = pAdapter->IpAddressList.IpAddress.String;
			adapterInfo["Netmask"] = pAdapter->IpAddressList.IpMask.String;
			adapterInfo["Gateway"] = pAdapter->GatewayList.IpAddress.String;
			
			uint32_t ip = inet_addr(pAdapter->GatewayList.IpAddress.String);
#else
	struct ifaddrs *ifaddr, *ifa;
	int family, n;
	char host[NI_MAXHOST];

	if (getifaddrs(&ifaddr) == -1) {
		perror("getifaddrs");
		exit(EXIT_FAILURE);
	}

	std::map<char*, char*> macs;
	char gatewayDefault[] = {'0', '0', '0', '.', '0', '0', '0', '.', '0', '0', '0', '.', '0', '0', '0', '\0'};

	for (ifa = ifaddr, n = 0; ifa != NULL; ifa = ifa->ifa_next, n++) {
		if (ifa->ifa_addr == NULL)
			continue;

		family = ifa->ifa_addr->sa_family;

		if (family == AF_INET || family == AF_INET6) {
			getnameinfo(ifa->ifa_addr,
				(family == AF_INET) ? sizeof(struct sockaddr_in) :
										sizeof(struct sockaddr_in6),
				host, NI_MAXHOST,
				NULL, 0, NI_NUMERICHOST);

        	auto adapterInfo = json::object();
        	adapterInfo["Name"] = ifa->ifa_name;

			char *mask = (char*)calloc((family == AF_INET) ? INET_ADDRSTRLEN: INET6_ADDRSTRLEN, sizeof(char*));
			char *broadcast = (char*)calloc((family == AF_INET) ? INET_ADDRSTRLEN: INET6_ADDRSTRLEN, sizeof(char*));

			const char *gateway = &gatewayDefault[0];
			if (family == AF_INET) {
				struct sockaddr_in *sa1 = (struct sockaddr_in *) ifa->ifa_netmask;
				struct sockaddr_in *sa2 = (struct sockaddr_in *) ifa->ifa_dstaddr;
				inet_ntop(AF_INET, &sa1->sin_addr, mask, INET_ADDRSTRLEN);
				inet_ntop(AF_INET, &sa2->sin_addr, broadcast, INET_ADDRSTRLEN);

				string s(host);
				string e = ".1\0";
				int last = findLastIndex(host, '.');
				s.replace(s.begin() + last, s.end(), e.begin(), e.end());
				gateway = s.c_str();
			} else {

				if(ifa->ifa_netmask) {
					struct sockaddr_in6 *sa1 = (struct sockaddr_in6*) ifa->ifa_netmask;
					inet_ntop(AF_INET6, &sa1->sin6_addr, mask, INET6_ADDRSTRLEN);
				}

				if((ifa->ifa_flags & IFF_BROADCAST) > 0 && ifa->ifa_dstaddr) {
					struct sockaddr_in6 *sa2 = (struct sockaddr_in6*) ifa->ifa_dstaddr;
					inet_ntop(AF_INET6, &sa2->sin6_addr, broadcast, INET6_ADDRSTRLEN);
				}
			}
			if (macs.find(ifa->ifa_name) == macs.end()) {
				macs[ifa->ifa_name] = NULL;
			} else {
				adapterInfo["Mac"] = macs[ifa->ifa_name];
			}
			adapterInfo["Ip"] = host;
			adapterInfo["Netmask"] = mask;
			adapterInfo["Broadcast"] = broadcast;
			adapterInfo["Gateway"] = gateway;

			uint32_t ip = inet_addr(gateway);
			free(mask);
			free(broadcast);
#endif
			//std::cout << std::to_string(ip) << " " << std::to_string(( ip & (255 << 24))) << std::endl;
			if(( ip & 255 ) == 10)
			{	
				cout << "M: " << gateway << endl;
				cout << "MATCH" << endl;
				adapters.push_back(adapterInfo);
			}
#if defined(_WIN32) || defined(_WIN64)
			pAdapter = pAdapter->Next;
		}
	} else {
		printf("GetAdaptersInfo failed with error: %ld\n", dwRetVal);
	}

	if (pAdapterInfo)
		FREE(pAdapterInfo);
#else
#ifdef __linux__
        } else if (family == AF_PACKET) {
#else
		} else if (family == AF_LINK) {
#endif
			char mac[18];
#ifdef __linux__
			struct sockaddr_ll *s = (struct sockaddr_ll*)(ifa->ifa_addr);
			int i;
			int len = 0;
			for (i = 0; i < 6; i++) {
				len += sprintf(mac+len, "%02X%s", s->sll_addr[i], i < 5 ? ":":"");
			}
#else
			unsigned char *ptr = (unsigned char *)LLADDR((struct sockaddr_dl *)(ifa)->ifa_addr);
			sprintf(mac, "%02x:%02x:%02x:%02x:%02x:%02x",
								*ptr, *(ptr+1), *(ptr+2), *(ptr+3), *(ptr+4), *(ptr+5));
#endif
			macs[ifa->ifa_name] = mac;
			if (macs.find(ifa->ifa_name) == macs.end()) {
				macs[ifa->ifa_name] = mac;
			} else {
				for (const auto &i: adapters) {
					if (i["Name"] == ifa->ifa_name && i["Mac"] == NULL) {
						i.get<json::object_t>()["Mac"] = mac;
					}
				}
			}
		}
    }

    freeifaddrs(ifaddr);
#endif
    return adapters;
}