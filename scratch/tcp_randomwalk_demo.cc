#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/log.h"
#include "ns3/attribute.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("DirectTcpConnection"); 


/*void ServerReceiveCallback(Ptr<Socket> socket)
{
    NS_LOG_UNCOND("ServerReceiveCallback function called on n1.");
    Ptr<Packet> packet;
    Address from;
    while ((packet = socket->RecvFrom(from)))
    {
        NS_LOG_INFO("Server n1 received a packet of size: " << packet->GetSize() << " bytes from " << InetSocketAddress::ConvertFrom(from).GetIpv4());
    }
}
*/

Time g_sessionStartTime; //测量端到端时间
Time g_sessionEndTime;

int main(int argc, char *argv[])
{
    LogComponentEnable("DirectTcpConnection", LOG_LEVEL_INFO);
    // LogComponentEnable("BulkSendApplication", LOG_LEVEL_INFO); //查看BulkSend的日志
    // LogComponentEnable("PacketSink", LOG_LEVEL_INFO);       // 用PacketSink替代ServerReceiveCallback
    // LogComponentEnable("TcpSocketBase", LOG_LEVEL_INFO);    // 查看TCP套接字状态

    NodeContainer nodes;
    nodes.Create(6); // n0: 客户端, n1: 服务器, n2-n5: 中间节点

    PointToPointHelper pointToPoint;
    pointToPoint.SetDeviceAttribute("DataRate", StringValue("10Mbps"));
    pointToPoint.SetChannelAttribute("Delay", StringValue("1ms"));

    // 链路创建保持不变
    NetDeviceContainer devices1 = pointToPoint.Install(NodeContainer(nodes.Get(0), nodes.Get(3))); // n0 到 n3
    NetDeviceContainer devices2 = pointToPoint.Install(NodeContainer(nodes.Get(3), nodes.Get(2))); // n3 到 n2
    NetDeviceContainer devices3 = pointToPoint.Install(NodeContainer(nodes.Get(3), nodes.Get(4))); // n3 到 n4
    NetDeviceContainer devices4 = pointToPoint.Install(NodeContainer(nodes.Get(3), nodes.Get(5))); // n3 到 n5
    NetDeviceContainer devices5 = pointToPoint.Install(NodeContainer(nodes.Get(2), nodes.Get(5))); // n2 到 n5
    NetDeviceContainer devices6 = pointToPoint.Install(NodeContainer(nodes.Get(4), nodes.Get(5))); // n4 到 n5
    NetDeviceContainer devices7 = pointToPoint.Install(NodeContainer(nodes.Get(5), nodes.Get(1))); // n5 到 n1

    InternetStackHelper stack;
    stack.Install(nodes);

    Ipv4AddressHelper address;
    address.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer interfaces1 = address.Assign(devices1);

    address.SetBase("10.1.2.0", "255.255.255.0");
    Ipv4InterfaceContainer interfaces2 = address.Assign(devices2);

    address.SetBase("10.1.3.0", "255.255.255.0");
    Ipv4InterfaceContainer interfaces3 = address.Assign(devices3);

    address.SetBase("10.1.4.0", "255.255.255.0");
    Ipv4InterfaceContainer interfaces4 = address.Assign(devices4);

    address.SetBase("10.1.5.0", "255.255.255.0");
    Ipv4InterfaceContainer interfaces5 = address.Assign(devices5);

    address.SetBase("10.1.6.0", "255.255.255.0");
    Ipv4InterfaceContainer interfaces6 = address.Assign(devices6);

    address.SetBase("10.1.7.0", "255.255.255.0");
    Ipv4InterfaceContainer interfaces7 = address.Assign(devices7); // n5-n1链路, interfaces7.GetAddress(1) 是 n1 的IP

    // 启用全局路由
    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    // 路由表打印
    Ptr<OutputStreamWrapper> routingStream = Create<OutputStreamWrapper>(&std::cout);
    for (uint32_t i = 0; i < nodes.GetN(); ++i)
    {
        NS_LOG_UNCOND("Routing table for node " << i << ":");
        Ipv4GlobalRoutingHelper::PrintRoutingTableAt(Seconds(1.0), nodes.Get(i), routingStream);
    }


    // 设置服务器 (n1)
    uint16_t port = 9;
    // PacketSinkHelper 通常用于服务器端接收数据，它会自动处理接收和统计
    PacketSinkHelper packetSinkHelper("ns3::TcpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), port));
    ApplicationContainer serverApps = packetSinkHelper.Install(nodes.Get(1));
    serverApps.Start(Seconds(1.0));
    //serverApps.Stop(Seconds(30.0));//避免提前结束
    NS_LOG_UNCOND("Server (n1) listening on port " << port);

    


    // 设置客户端 (n0)
    // 目标地址现在是服务器 n1 的 IP 地址 (interfaces7.GetAddress(1))
    BulkSendHelper clientHelper("ns3::TcpSocketFactory", InetSocketAddress(interfaces7.GetAddress(1), port));
    uint64_t clientMaxBytes = 1000000; // 发送1MB数据（后续可调整）
    clientHelper.SetAttribute("MaxBytes", UintegerValue(clientMaxBytes)); // 发送1MB数据
    clientHelper.SetAttribute("SendSize", UintegerValue(1024));   // 每次发送1024字节
    ApplicationContainer clientApps = clientHelper.Install(nodes.Get(0));
    clientApps.Start(Seconds(2.0));
    clientApps.Stop(Seconds(30.0));
    NS_LOG_UNCOND("Client (n0) started sending data to n1 (" << interfaces7.GetAddress(1) << ":" << port << ")");


   

    // 启用抓包功能 
    // 可以在n0和n1上抓包，或者在中间节点上观察数据包是否被正确路由
    pointToPoint.EnablePcapAll("direct-tcp-connection", true); // 在所有设备上启用pcap

    Simulator::Run();
    NS_LOG_INFO("Simulation finished.");

    uint64_t expectedBytes = clientMaxBytes;
    if (serverApps.GetN() > 0) {
        Ptr<Application> app = serverApps.Get(0);
        Ptr<PacketSink> sink = DynamicCast<PacketSink>(app);
        if (sink) {
            uint64_t totalBytesReceived = sink->GetTotalRx();
            NS_LOG_UNCOND("----------------------------------------------------");
            NS_LOG_UNCOND("TCP Data Transfer Summary:");
            NS_LOG_UNCOND("  Client (n0) intended to send: " << expectedBytes << " bytes."); // 使用 expectedBytes
            NS_LOG_UNCOND("  Server (n1) PacketSink received: " << totalBytesReceived << " bytes.");

            if (expectedBytes > 0 && totalBytesReceived >= expectedBytes) {
                NS_LOG_UNCOND("  Status: SUCCESS - All expected bytes were received.");
            } else if (expectedBytes > 0 && totalBytesReceived < expectedBytes) {
                NS_LOG_UNCOND("  Status: PARTIAL - Only " << totalBytesReceived << " out of " << expectedBytes << " bytes received.");
            } else if (totalBytesReceived > 0) {
                NS_LOG_UNCOND("  Status: DATA RECEIVED - Server received " << totalBytesReceived << " bytes (client's sent amount not definitively known or was zero).");
            }
            else {
                NS_LOG_UNCOND("  Status: NO DATA RECEIVED by PacketSink.");
            }

            NS_LOG_UNCOND("----------------------------------------------------");
        } else {
            NS_LOG_WARN("Could not get PacketSink application on server to verify session completion.");
            NS_LOG_UNCOND("TCP Data Transfer Session: Status Unknown (Could not access PacketSink).");
        }
    } else {
        NS_LOG_WARN("No server application found to verify session completion.");
        NS_LOG_UNCOND("TCP Data Transfer Session: Status Unknown (No server application).");
    }  

    Simulator::Destroy();
    return 0;
}