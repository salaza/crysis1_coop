#ifndef _CoopNetPlatform_H_
#define _CoopNetPlatform_H_

// Crysis Co-op: GameSpy-free internet hosting helpers.
// This module is pure Win32 (no CryEngine headers) so it can safely include winsock2/wininet/UPnP.

#include <string>
#include <vector>

namespace CoopNet
{
	enum EStatus
	{
		eS_Idle = 0,
		eS_Pending,
		eS_Ok,
		eS_Failed,
	};

	struct SServerEntry
	{
		std::string host;   // as written by the user (IP or hostname)
		std::string ip;     // resolved IPv4
		int         port;
	};

	// Starts (asynchronously) UPnP port forwarding of the UDP game port and public IP detection.
	void StartHostServices(int port, bool bUseUPnP);
	// Removes the UPnP port mapping created by StartHostServices (blocking, short).
	void StopHostServices();

	// Polled from the main thread. Returns true exactly once after the worker finished.
	bool PollHostServicesFinished();
	EStatus GetUPnPStatus();
	EStatus GetPublicIPStatus();
	std::string GetPublicIP();
	std::string GetLocalIP();
	std::string GetUPnPError();

	// Name of the VPN adapter carrying the internet traffic (empty if no VPN is active).
	std::string GetVPNName();
	// True if the router's WAN address is itself private (double NAT / carrier-grade NAT).
	bool IsDoubleNAT();
	std::string GetRouterExternalIP();

	// Parses "host", "host:port" or "host port" and resolves the host to an IPv4 address.
	bool ParseAndResolve(const char* address, int defaultPort, SServerEntry& out);

	// Server list file (Mods/CrysisCoop/coop_servers.txt), one "host[:port]" per line.
	void LoadServerList(std::vector<SServerEntry>& out, int defaultPort);
	bool AddToServerList(const char* address);

	bool CopyToClipboard(const std::string& text);
}

#endif // _CoopNetPlatform_H_
