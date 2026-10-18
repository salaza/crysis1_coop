// Crysis Co-op: GameSpy-free internet hosting helpers (UPnP, public IP detection, server list).
// Intentionally does not include any CryEngine header (see CoopNetPlatform.h).

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <wininet.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <cwctype>
#include <mutex>
#include <thread>

#include "CoopNetPlatform.h"

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "user32.lib")

namespace CoopNet
{

static const char* SERVER_LIST_FILE = "Mods\\CrysisCoop\\coop_servers.txt";
static const wchar_t* UPNP_DESCRIPTION = L"Crysis Co-op";

static std::mutex        s_mutex;
static std::thread       s_worker;
static std::atomic<bool> s_workerDone(false);
static std::atomic<bool> s_finishedReported(true);

static EStatus     s_upnpStatus = eS_Idle;
static EStatus     s_publicIPStatus = eS_Idle;
static std::string s_publicIP;
static std::string s_localIP;
static std::string s_upnpError;
static std::string s_vpnName;
static int         s_mappedPort = 0;

//------------------------------------------------------------------------
static void EnsureWinsock()
{
	static bool s_init = false;
	if (!s_init)
	{
		WSADATA data;
		WSAStartup(MAKEWORD(2, 2), &data); // ref-counted, safe even if the engine already did it
		s_init = true;
	}
}

//------------------------------------------------------------------------
static std::string Trim(const std::string& s)
{
	size_t b = s.find_first_not_of(" \t\r\n");
	if (b == std::string::npos)
		return std::string();
	size_t e = s.find_last_not_of(" \t\r\n");
	return s.substr(b, e - b + 1);
}

//------------------------------------------------------------------------
static std::string DetectRoutedIP()
{
	EnsureWinsock();

	// "Connect" a UDP socket towards a public address: no packet is sent, but the OS picks
	// the interface that currently routes to the internet (this is the VPN if one is active).
	std::string result;
	SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (s == INVALID_SOCKET)
		return result;

	sockaddr_in remote = {};
	remote.sin_family = AF_INET;
	remote.sin_port = htons(53);
	inet_pton(AF_INET, "8.8.8.8", &remote.sin_addr);

	if (connect(s, (sockaddr*)&remote, sizeof(remote)) == 0)
	{
		sockaddr_in local = {};
		int len = sizeof(local);
		if (getsockname(s, (sockaddr*)&local, &len) == 0)
		{
			char buf[INET_ADDRSTRLEN] = {0};
			inet_ntop(AF_INET, &local.sin_addr, buf, sizeof(buf));
			result = buf;
		}
	}
	closesocket(s);
	return result;
}

//------------------------------------------------------------------------
static bool ContainsI(const std::wstring& hay, const wchar_t* needle)
{
	std::wstring h = hay, n = needle;
	for (auto& c : h) c = (wchar_t)towlower(c);
	for (auto& c : n) c = (wchar_t)towlower(c);
	return h.find(n) != std::wstring::npos;
}

static bool IsVirtualOrVPNAdapter(const IP_ADAPTER_ADDRESSES* a)
{
	if (a->IfType == IF_TYPE_PPP || a->IfType == IF_TYPE_TUNNEL || a->IfType == IF_TYPE_PROP_VIRTUAL)
		return true;

	static const wchar_t* keywords[] =
	{
		L"vpn", L"tap-", L"tap ", L"wintun", L"wireguard", L"proton", L"nord", L"openvpn", L"mullvad",
		L"tunnel", L"virtualbox", L"vmware", L"hyper-v", L"vethernet", L"hamachi", L"zerotier", L"radmin",
		L"tailscale", L"cisco", L"fortinet", L"surfshark", L"expressvpn", L"cyberghost", L"windscribe",
	};
	std::wstring desc = a->Description ? a->Description : L"";
	std::wstring name = a->FriendlyName ? a->FriendlyName : L"";
	for (size_t i = 0; i < sizeof(keywords) / sizeof(keywords[0]); ++i)
	{
		if (ContainsI(desc, keywords[i]) || ContainsI(name, keywords[i]))
			return true;
	}
	return false;
}

// Returns the IPv4 of the physical adapter connected to the router and, if the
// internet route goes through a different (VPN) adapter, that adapter's name.
static std::string DetectLocalIP(std::string* pVpnName = NULL)
{
	EnsureWinsock();

	std::string routedIP = DetectRoutedIP();
	std::string lanIP, routedAdapterName;
	bool routedIsVirtual = false;

	ULONG size = 32 * 1024;
	std::string buffer(size, '\0');
	ULONG flags = GAA_FLAG_INCLUDE_GATEWAYS | GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
	ULONG ret = GetAdaptersAddresses(AF_INET, flags, NULL, (IP_ADAPTER_ADDRESSES*)&buffer[0], &size);
	if (ret == ERROR_BUFFER_OVERFLOW)
	{
		buffer.assign(size, '\0');
		ret = GetAdaptersAddresses(AF_INET, flags, NULL, (IP_ADAPTER_ADDRESSES*)&buffer[0], &size);
	}

	if (ret == NO_ERROR)
	{
		for (IP_ADAPTER_ADDRESSES* a = (IP_ADAPTER_ADDRESSES*)&buffer[0]; a; a = a->Next)
		{
			if (a->OperStatus != IfOperStatusUp || a->IfType == IF_TYPE_SOFTWARE_LOOPBACK)
				continue;

			bool isVirtual = IsVirtualOrVPNAdapter(a);
			for (IP_ADAPTER_UNICAST_ADDRESS* u = a->FirstUnicastAddress; u; u = u->Next)
			{
				char buf[INET_ADDRSTRLEN] = {0};
				inet_ntop(AF_INET, &((sockaddr_in*)u->Address.lpSockaddr)->sin_addr, buf, sizeof(buf));

				if (routedIP == buf)
				{
					routedIsVirtual = isVirtual;
					char name[256] = {0};
					WideCharToMultiByte(CP_ACP, 0, a->FriendlyName ? a->FriendlyName : L"VPN", -1, name, sizeof(name), NULL, NULL);
					routedAdapterName = name;
				}

				// physical adapter with a default gateway = the one connected to the router
				if (lanIP.empty() && !isVirtual && a->FirstGatewayAddress)
					lanIP = buf;
			}
		}
	}

	if (lanIP.empty())
		lanIP = routedIP;

	if (pVpnName)
	{
		bool vpnActive = routedIsVirtual || (!routedIP.empty() && routedIP != lanIP);
		*pVpnName = vpnActive ? (routedAdapterName.empty() ? std::string("VPN") : routedAdapterName) : std::string();
	}

	return lanIP;
}

//------------------------------------------------------------------------
static bool HttpGet(const char* url, std::string& out)
{
	out.clear();
	HINTERNET hNet = InternetOpenA("CrysisCoop", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
	if (!hNet)
		return false;

	DWORD timeout = 5000;
	InternetSetOptionA(hNet, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
	InternetSetOptionA(hNet, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));

	HINTERNET hUrl = InternetOpenUrlA(hNet, url, NULL, 0, INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_NO_UI, 0);
	if (hUrl)
	{
		char buf[256];
		DWORD read = 0;
		while (InternetReadFile(hUrl, buf, sizeof(buf), &read) && read > 0 && out.size() < 1024)
			out.append(buf, read);
		InternetCloseHandle(hUrl);
	}
	InternetCloseHandle(hNet);

	out = Trim(out);
	return !out.empty();
}

//------------------------------------------------------------------------
static bool IsIPv4(const std::string& s)
{
	in_addr addr;
	return !s.empty() && s.size() < 16 && inet_pton(AF_INET, s.c_str(), &addr) == 1;
}

//------------------------------------------------------------------------
static std::string DetectPublicIP()
{
	static const char* services[] =
	{
		"http://api.ipify.org",
		"http://checkip.amazonaws.com",
		"http://icanhazip.com",
	};

	for (size_t i = 0; i < sizeof(services) / sizeof(services[0]); ++i)
	{
		std::string reply;
		if (HttpGet(services[i], reply) && IsIPv4(reply))
			return reply;
	}
	return std::string();
}

//------------------------------------------------------------------------
// Minimal UPnP IGD client (SSDP discovery + SOAP), bound to the LAN adapter.
//------------------------------------------------------------------------
struct SIgd
{
	std::string host;        // router host
	int         port = 80;
	std::string controlPath; // e.g. /ctl/IPConn
	std::string serviceType; // urn:schemas-upnp-org:service:WANIPConnection:1
};

static SIgd        s_igd;
static bool        s_igdValid = false;
static bool        s_doubleNAT = false;
static std::string s_routerExternalIP;

static bool IsPrivateIPv4(const std::string& ip)
{
	unsigned a = 0, b = 0, c = 0, d = 0;
	if (sscanf_s(ip.c_str(), "%u.%u.%u.%u", &a, &b, &c, &d) != 4)
		return false;
	return a == 10 || (a == 172 && b >= 16 && b <= 31) || (a == 192 && b == 168) ||
		(a == 100 && b >= 64 && b <= 127) /* CGNAT */ || a == 0 || a == 127 || (a == 169 && b == 254);
}

static std::string ToLower(std::string s)
{
	for (auto& c : s) c = (char)tolower((unsigned char)c);
	return s;
}

static std::string XmlValue(const std::string& xml, const std::string& tag, size_t from = 0, size_t* pEnd = NULL)
{
	// tolerant of namespace prefixes: matches <tag> or <ns:tag>
	std::string lower = ToLower(xml);
	std::string ltag = ToLower(tag);
	size_t pos = from;
	while ((pos = lower.find(ltag + ">", pos)) != std::string::npos)
	{
		size_t lt = lower.rfind('<', pos);
		if (lt != std::string::npos && lower[lt + 1] != '/' && (lt + 1 == pos || lower[pos - 1] == ':'))
		{
			size_t start = pos + ltag.size() + 1;
			size_t end = lower.find("</", start);
			if (end == std::string::npos)
				return std::string();
			if (pEnd)
				*pEnd = end;
			return Trim(xml.substr(start, end - start));
		}
		pos += ltag.size();
	}
	return std::string();
}

static bool ParseUrl(const std::string& url, std::string& host, int& port, std::string& path)
{
	std::string u = url;
	size_t p = u.find("://");
	if (p != std::string::npos)
		u = u.substr(p + 3);
	size_t slash = u.find('/');
	std::string hostport = slash == std::string::npos ? u : u.substr(0, slash);
	path = slash == std::string::npos ? "/" : u.substr(slash);
	size_t colon = hostport.find(':');
	host = colon == std::string::npos ? hostport : hostport.substr(0, colon);
	port = colon == std::string::npos ? 80 : atoi(hostport.substr(colon + 1).c_str());
	return !host.empty() && port > 0;
}

static bool HttpRequest(const std::string& host, int port, const std::string& path, const char* verb,
	const std::string& headers, const std::string& body, std::string& response, DWORD& status)
{
	response.clear();
	status = 0;
	HINTERNET hNet = InternetOpenA("CrysisCoop", INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0);
	if (!hNet)
		return false;

	DWORD timeout = 4000;
	InternetSetOptionA(hNet, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
	InternetSetOptionA(hNet, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));
	InternetSetOptionA(hNet, INTERNET_OPTION_SEND_TIMEOUT, &timeout, sizeof(timeout));

	bool ok = false;
	HINTERNET hConn = InternetConnectA(hNet, host.c_str(), (INTERNET_PORT)port, NULL, NULL, INTERNET_SERVICE_HTTP, 0, 0);
	if (hConn)
	{
		HINTERNET hReq = HttpOpenRequestA(hConn, verb, path.c_str(), NULL, NULL, NULL,
			INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_NO_UI | INTERNET_FLAG_KEEP_CONNECTION, 0);
		if (hReq)
		{
			if (HttpSendRequestA(hReq, headers.empty() ? NULL : headers.c_str(), (DWORD)headers.size(),
				body.empty() ? NULL : (LPVOID)body.c_str(), (DWORD)body.size()))
			{
				DWORD len = sizeof(status);
				HttpQueryInfoA(hReq, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &status, &len, NULL);
				char buf[1024];
				DWORD read = 0;
				while (InternetReadFile(hReq, buf, sizeof(buf), &read) && read > 0 && response.size() < 256 * 1024)
					response.append(buf, read);
				ok = true;
			}
			InternetCloseHandle(hReq);
		}
		InternetCloseHandle(hConn);
	}
	InternetCloseHandle(hNet);
	return ok;
}

static bool SoapCall(const SIgd& igd, const char* action, const std::string& args, std::string& response)
{
	std::string body =
		"<?xml version=\"1.0\"?>\r\n"
		"<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
		"<s:Body><u:" + std::string(action) + " xmlns:u=\"" + igd.serviceType + "\">" + args +
		"</u:" + std::string(action) + "></s:Body></s:Envelope>\r\n";
	std::string headers =
		"Content-Type: text/xml; charset=\"utf-8\"\r\n"
		"SOAPAction: \"" + igd.serviceType + "#" + action + "\"\r\n";

	DWORD status = 0;
	if (!HttpRequest(igd.host, igd.port, igd.controlPath, "POST", headers, body, response, status))
		return false;
	return status == 200;
}

static bool FindWanService(const std::string& location, SIgd& out)
{
	std::string host, path;
	int port = 80;
	if (!ParseUrl(location, host, port, path))
		return false;

	std::string xml;
	DWORD status = 0;
	if (!HttpRequest(host, port, path, "GET", "", "", xml, status) || status != 200)
		return false;

	std::string baseHost = host;
	int basePort = port;
	std::string urlBase = XmlValue(xml, "URLBase");
	if (!urlBase.empty())
	{
		std::string p;
		ParseUrl(urlBase, baseHost, basePort, p);
	}

	static const char* wanTypes[] =
	{
		"urn:schemas-upnp-org:service:WANIPConnection:2",
		"urn:schemas-upnp-org:service:WANIPConnection:1",
		"urn:schemas-upnp-org:service:WANPPPConnection:1",
	};

	for (size_t t = 0; t < sizeof(wanTypes) / sizeof(wanTypes[0]); ++t)
	{
		size_t pos = xml.find(std::string(wanTypes[t]) + "<");
		if (pos == std::string::npos)
			continue;

		size_t serviceEnd = ToLower(xml).find("</service>", pos);
		std::string service = xml.substr(pos, serviceEnd == std::string::npos ? std::string::npos : serviceEnd - pos);
		std::string control = XmlValue("<x>" + service, "controlURL");
		if (control.empty())
			continue;

		out.serviceType = wanTypes[t];
		if (control.find("://") != std::string::npos)
		{
			ParseUrl(control, out.host, out.port, out.controlPath);
		}
		else
		{
			out.host = baseHost;
			out.port = basePort;
			out.controlPath = control[0] == '/' ? control : "/" + control;
		}
		return true;
	}
	return false;
}

static bool DiscoverIGD(const std::string& lanIP, SIgd& out)
{
	EnsureWinsock();

	SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (s == INVALID_SOCKET)
		return false;

	// Bind to the physical LAN adapter so the SSDP multicast doesn't go into a VPN tunnel.
	sockaddr_in local = {};
	local.sin_family = AF_INET;
	inet_pton(AF_INET, lanIP.c_str(), &local.sin_addr);
	bind(s, (sockaddr*)&local, sizeof(local));
	setsockopt(s, IPPROTO_IP, IP_MULTICAST_IF, (const char*)&local.sin_addr, sizeof(local.sin_addr));
	DWORD ttl = 2;
	setsockopt(s, IPPROTO_IP, IP_MULTICAST_TTL, (const char*)&ttl, sizeof(ttl));
	DWORD timeout = 1000;
	setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));

	sockaddr_in mcast = {};
	mcast.sin_family = AF_INET;
	mcast.sin_port = htons(1900);
	inet_pton(AF_INET, "239.255.255.250", &mcast.sin_addr);

	static const char* searchTargets[] =
	{
		"urn:schemas-upnp-org:device:InternetGatewayDevice:1",
		"urn:schemas-upnp-org:device:InternetGatewayDevice:2",
		"urn:schemas-upnp-org:service:WANIPConnection:1",
		"urn:schemas-upnp-org:service:WANPPPConnection:1",
	};

	std::vector<std::string> locations;
	for (int attempt = 0; attempt < 3 && locations.empty(); ++attempt)
	{
		for (size_t i = 0; i < sizeof(searchTargets) / sizeof(searchTargets[0]); ++i)
		{
			char msg[512];
			sprintf_s(msg, "M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\nMAN: \"ssdp:discover\"\r\nMX: 2\r\nST: %s\r\n\r\n", searchTargets[i]);
			sendto(s, msg, (int)strlen(msg), 0, (sockaddr*)&mcast, sizeof(mcast));
		}

		DWORD start = GetTickCount();
		while (GetTickCount() - start < 2500)
		{
			char buf[2048];
			int n = recv(s, buf, sizeof(buf) - 1, 0);
			if (n <= 0)
				continue;
			buf[n] = 0;
			std::string reply(buf);
			std::string lower = ToLower(reply);
			size_t p = lower.find("\nlocation:");
			if (p == std::string::npos)
				continue;
			size_t e = reply.find("\r", p + 10);
			std::string loc = Trim(reply.substr(p + 10, e == std::string::npos ? std::string::npos : e - p - 10));
			bool known = false;
			for (size_t k = 0; k < locations.size(); ++k)
				known = known || locations[k] == loc;
			if (!known)
				locations.push_back(loc);
		}
	}
	closesocket(s);

	for (size_t i = 0; i < locations.size(); ++i)
	{
		if (FindWanService(locations[i], out))
			return true;
	}
	return false;
}

static bool AddUPnPMapping(int port, const std::string& localIP, std::string& error, std::string& externalIP)
{
	SIgd igd;
	if (!DiscoverIGD(localIP, igd))
	{
		error = "no UPnP router found (enable UPnP in your router settings, or forward the port manually)";
		return false;
	}

	char args[1024];
	sprintf_s(args,
		"<NewRemoteHost></NewRemoteHost><NewExternalPort>%d</NewExternalPort><NewProtocol>UDP</NewProtocol>"
		"<NewInternalPort>%d</NewInternalPort><NewInternalClient>%s</NewInternalClient><NewEnabled>1</NewEnabled>"
		"<NewPortMappingDescription>Crysis Co-op</NewPortMappingDescription><NewLeaseDuration>0</NewLeaseDuration>",
		port, port, localIP.c_str());

	std::string response;
	if (!SoapCall(igd, "AddPortMapping", args, response))
	{
		// some routers reject an existing mapping - delete and retry once
		char del[256];
		sprintf_s(del, "<NewRemoteHost></NewRemoteHost><NewExternalPort>%d</NewExternalPort><NewProtocol>UDP</NewProtocol>", port);
		std::string dummy;
		SoapCall(igd, "DeletePortMapping", del, dummy);
		if (!SoapCall(igd, "AddPortMapping", args, response))
		{
			std::string code = XmlValue(response, "errorDescription");
			error = "router refused the port mapping" + (code.empty() ? std::string() : " (" + code + ")");
			return false;
		}
	}

	if (SoapCall(igd, "GetExternalIPAddress", "", response))
		externalIP = XmlValue(response, "NewExternalIPAddress");

	std::lock_guard<std::mutex> lock(s_mutex);
	s_igd = igd;
	s_igdValid = true;
	return true;
}

static void RemoveUPnPMapping(int port)
{
	SIgd igd;
	{
		std::lock_guard<std::mutex> lock(s_mutex);
		if (!s_igdValid)
			return;
		igd = s_igd;
	}
	char del[256];
	sprintf_s(del, "<NewRemoteHost></NewRemoteHost><NewExternalPort>%d</NewExternalPort><NewProtocol>UDP</NewProtocol>", port);
	std::string dummy;
	SoapCall(igd, "DeletePortMapping", del, dummy);
}

//------------------------------------------------------------------------
static void HostWorker(int port, bool bUseUPnP)
{
	std::string vpnName;
	std::string localIP = DetectLocalIP(&vpnName);
	{
		std::lock_guard<std::mutex> lock(s_mutex);
		s_localIP = localIP;
		s_vpnName = vpnName;
	}

	std::string routerIP;
	if (bUseUPnP)
	{
		std::string error;
		bool ok = !localIP.empty() && AddUPnPMapping(port, localIP, error, routerIP);
		if (localIP.empty())
			error = "could not determine the local IP address";

		std::lock_guard<std::mutex> lock(s_mutex);
		s_upnpStatus = ok ? eS_Ok : eS_Failed;
		s_upnpError = error;
		if (ok)
			s_mappedPort = port;
	}

	// The router's own WAN address is authoritative. The web lookup goes through the
	// VPN if one is active and would return the VPN server's address instead.
	std::string webIP = DetectPublicIP();
	std::string publicIP = (!routerIP.empty() && !IsPrivateIPv4(routerIP)) ? routerIP : webIP;

	{
		std::lock_guard<std::mutex> lock(s_mutex);
		s_routerExternalIP = routerIP;
		s_doubleNAT = !routerIP.empty() && IsPrivateIPv4(routerIP);
		s_publicIP = publicIP;
		s_publicIPStatus = publicIP.empty() ? eS_Failed : eS_Ok;
	}

	s_workerDone = true;
}

//------------------------------------------------------------------------
void StartHostServices(int port, bool bUseUPnP)
{
	if (s_worker.joinable())
	{
		if (!s_workerDone)
			return; // already running
		s_worker.join();
	}

	{
		std::lock_guard<std::mutex> lock(s_mutex);
		if (s_mappedPort == port && s_publicIPStatus == eS_Ok)
		{
			// already set up for this port during this session
			s_finishedReported = false;
			s_workerDone = true;
			return;
		}
		s_upnpStatus = bUseUPnP ? eS_Pending : eS_Idle;
		s_publicIPStatus = eS_Pending;
		s_upnpError.clear();
	}

	s_workerDone = false;
	s_finishedReported = false;
	s_worker = std::thread(HostWorker, port, bUseUPnP);
}

//------------------------------------------------------------------------
void StopHostServices()
{
	if (s_worker.joinable())
	{
		if (s_workerDone)
			s_worker.join();
		else
			s_worker.detach(); // don't block game shutdown on a slow router
	}

	int port = 0;
	{
		std::lock_guard<std::mutex> lock(s_mutex);
		port = s_mappedPort;
		s_mappedPort = 0;
	}

	if (port)
		RemoveUPnPMapping(port);
}

std::string GetVPNName()     { std::lock_guard<std::mutex> lock(s_mutex); return s_vpnName; }
bool IsDoubleNAT()           { std::lock_guard<std::mutex> lock(s_mutex); return s_doubleNAT; }
std::string GetRouterExternalIP() { std::lock_guard<std::mutex> lock(s_mutex); return s_routerExternalIP; }
//------------------------------------------------------------------------
bool PollHostServicesFinished()
{
	if (s_workerDone && !s_finishedReported)
	{
		s_finishedReported = true;
		return true;
	}
	return false;
}

EStatus GetUPnPStatus()      { std::lock_guard<std::mutex> lock(s_mutex); return s_upnpStatus; }
EStatus GetPublicIPStatus()  { std::lock_guard<std::mutex> lock(s_mutex); return s_publicIPStatus; }
std::string GetPublicIP()    { std::lock_guard<std::mutex> lock(s_mutex); return s_publicIP; }
std::string GetUPnPError()   { std::lock_guard<std::mutex> lock(s_mutex); return s_upnpError; }

std::string GetLocalIP()
{
	{
		std::lock_guard<std::mutex> lock(s_mutex);
		if (!s_localIP.empty())
			return s_localIP;
	}
	return DetectLocalIP();
}

//------------------------------------------------------------------------
bool ParseAndResolve(const char* address, int defaultPort, SServerEntry& out)
{
	if (!address)
		return false;

	std::string addr = Trim(address);
	if (addr.empty())
		return false;

	int port = defaultPort;
	size_t sep = addr.find_last_of(": ");
	if (sep != std::string::npos)
	{
		int p = atoi(addr.substr(sep + 1).c_str());
		if (p > 0 && p < 65536)
			port = p;
		addr = Trim(addr.substr(0, sep));
	}
	if (addr.empty())
		return false;

	EnsureWinsock();

	addrinfo hints = {};
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_DGRAM;
	addrinfo* result = NULL;
	if (getaddrinfo(addr.c_str(), NULL, &hints, &result) != 0 || !result)
		return false;

	char buf[INET_ADDRSTRLEN] = {0};
	inet_ntop(AF_INET, &((sockaddr_in*)result->ai_addr)->sin_addr, buf, sizeof(buf));
	freeaddrinfo(result);

	out.host = addr;
	out.ip = buf;
	out.port = port;
	return true;
}

//------------------------------------------------------------------------
void LoadServerList(std::vector<SServerEntry>& out, int defaultPort)
{
	out.clear();
	FILE* f = NULL;
	if (fopen_s(&f, SERVER_LIST_FILE, "r") != 0 || !f)
		return;

	char line[512];
	while (fgets(line, sizeof(line), f))
	{
		std::string l = Trim(line);
		if (l.empty() || l[0] == '#' || l[0] == ';' || (l.size() > 1 && l[0] == '/' && l[1] == '/'))
			continue;

		SServerEntry entry;
		if (ParseAndResolve(l.c_str(), defaultPort, entry))
			out.push_back(entry);
	}
	fclose(f);
}

//------------------------------------------------------------------------
bool AddToServerList(const char* address)
{
	if (!address || !*address)
		return false;

	std::string wanted = Trim(address);

	// already listed?
	FILE* f = NULL;
	bool exists = false;
	bool fileExists = false;
	if (fopen_s(&f, SERVER_LIST_FILE, "r") == 0 && f)
	{
		fileExists = true;
		char line[512];
		while (fgets(line, sizeof(line), f))
		{
			if (_stricmp(Trim(line).c_str(), wanted.c_str()) == 0)
			{
				exists = true;
				break;
			}
		}
		fclose(f);
	}
	if (exists)
		return true;

	if (fopen_s(&f, SERVER_LIST_FILE, "a") != 0 || !f)
		return false;

	if (!fileExists)
	{
		fputs("# Crysis Co-op internet server list (GameSpy replacement)\n", f);
		fputs("# One server per line: <ip or hostname>[:port]   (default port 64087)\n", f);
		fputs("# These servers are queried and shown in Multiplayer -> LAN when you press Refresh.\n", f);
	}
	fprintf(f, "%s\n", wanted.c_str());
	fclose(f);
	return true;
}

//------------------------------------------------------------------------
bool CopyToClipboard(const std::string& text)
{
	if (!OpenClipboard(NULL))
		return false;

	bool ok = false;
	EmptyClipboard();
	HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, text.size() + 1);
	if (hMem)
	{
		memcpy(GlobalLock(hMem), text.c_str(), text.size() + 1);
		GlobalUnlock(hMem);
		ok = SetClipboardData(CF_TEXT, hMem) != NULL;
		if (!ok)
			GlobalFree(hMem);
	}
	CloseClipboard();
	return ok;
}

} // namespace CoopNet
