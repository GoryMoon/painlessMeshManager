/*
 * meshDetect.cpp
 *
 *  Created on: 29.01.2018
 *      Author: admin
 */


#include <winsock2.h>
#include <iphlpapi.h>
#include <stdlib.h>
#include <stdio.h>
#include <iostream>

#include <boost/format.hpp>

#include "json.hpp"

#define MALLOC(x) HeapAlloc(GetProcessHeap(), 0, (x))
#define FREE(x) HeapFree(GetProcessHeap(), 0, (x))

using json = nlohmann::json;

//int findMesh(std::string &jsonResul, uint8_t match = 0)
//int findMesh(json& mesh, uint8_t match = 0)
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

    PIP_ADAPTER_INFO pAdapterInfo;
    PIP_ADAPTER_INFO pAdapter = NULL;
    DWORD dwRetVal = 0;
    UINT i;
    json adapters;

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
			std::cout << std::to_string(ip) << " " << std::to_string(( ip & (255 << 24))) << std::endl;
			if( ( ip & 255 ) == 10)
			{
				std::cout << "MATCH" << std::endl;
				adapters.push_back(adapterInfo);
			}

            pAdapter = pAdapter->Next;
        }
    } else {
        printf("GetAdaptersInfo failed with error: %ld\n", dwRetVal);
    }

    if (pAdapterInfo)
        FREE(pAdapterInfo);



    return adapters;
}
