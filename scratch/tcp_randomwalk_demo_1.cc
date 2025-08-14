#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/random-variable-stream.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("TcpRandomForwarding");

// 全局状态
Time g_sessionStartTime;
Time g_sessionEndTime;
std::map<Ptr<Socket>, Ptr<Packet>> g_pendingPackets; // 待转发数据包缓存

// 回调函数声明
void ConnectionSucceeded(Ptr<Socket> socket);
void ConnectionFailed(Ptr<Socket> socket);
void DataSent(Ptr<Socket> socket, uint32_t available);
void AcceptCallback(Ptr<Socket> socket, const Address& from);
void ServerReceive(Ptr<Socket> socket);
void DataReceived(Ptr<Socket> socket);
void ForwardToNextHop(Ptr<Socket> incomingSocket, Ptr<Packet> packet, Ipv4Address nextHop);

int main(int argc, char *argv[])
{
    LogComponentEnable("TcpRandomForwarding", LOG_LEVEL_INFO);
    
    // 创建节点和拓扑
    NodeContainer nodes;
    nodes.Create(6);
    
    PointToPointHelper pointToPoint;
    pointToPoint.SetDeviceAttribute("DataRate", StringValue("10Mbps"));
    pointToPoint.SetChannelAttribute("Delay", StringValue("1ms"));
    
    NetDeviceContainer d01 = pointToPoint.Install(nodes.Get(0), nodes.Get(3));
    NetDeviceContainer d32 = pointToPoint.Install(nodes.Get(3), nodes.Get(2));
    NetDeviceContainer d34 = pointToPoint.Install(nodes.Get(3), nodes.Get(4));
    NetDeviceContainer d35 = pointToPoint.Install(nodes.Get(3), nodes.Get(5));
    NetDeviceContainer d25 = pointToPoint.Install(nodes.Get(2), nodes.Get(5));
    NetDeviceContainer d45 = pointToPoint.Install(nodes.Get(4), nodes.Get(5));
    NetDeviceContainer d51 = pointToPoint.Install(nodes.Get(5), nodes.Get(1));

    InternetStackHelper stack;
    stack.Install(nodes);
    
    Ipv4AddressHelper ipv4;
    ipv4.SetBase("10.1.1.0", "255.255.255.0"); Ipv4InterfaceContainer i01 = ipv4.Assign(d01);
    ipv4.SetBase("10.1.2.0", "255.255.255.0"); Ipv4InterfaceContainer i32 = ipv4.Assign(d32);
    ipv4.SetBase("10.1.3.0", "255.255.255.0"); Ipv4InterfaceContainer i34 = ipv4.Assign(d34);
    ipv4.SetBase("10.1.4.0", "255.255.255.0"); Ipv4InterfaceContainer i35 = ipv4.Assign(d35);
    ipv4.SetBase("10.1.5.0", "255.255.255.0"); Ipv4InterfaceContainer i25 = ipv4.Assign(d25);
    ipv4.SetBase("10.1.6.0", "255.255.255.0"); Ipv4InterfaceContainer i45 = ipv4.Assign(d45);
    ipv4.SetBase("10.1.7.0", "255.255.255.0"); Ipv4InterfaceContainer i51 = ipv4.Assign(d51);

    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    // 服务器 (n1)
    uint16_t port = 5000;
    Ptr<Socket> serverSocket = Socket::CreateSocket(nodes.Get(1), TypeId::LookupByName("ns3::TcpSocketFactory"));
    serverSocket->Bind(InetSocketAddress(Ipv4Address::GetAny(), port));
    serverSocket->Listen();
    serverSocket->SetAcceptCallback(MakeNullCallback<bool, Ptr<Socket>, const Address &>(), 
                                    MakeCallback(&AcceptCallback));

    // 客户端 (n0)
    BulkSendHelper client("ns3::TcpSocketFactory", InetSocketAddress(i01.GetAddress(1), port));
    client.SetAttribute("MaxBytes", UintegerValue(100000));
    ApplicationContainer clientApp = client.Install(nodes.Get(0));
    clientApp.Start(Seconds(2.0));
    clientApp.Stop(Seconds(10.0));

    // 中间节点 (n2, n3, n4, n5)
    for (uint32_t i : {2, 3, 4, 5}) { // n2, n3, n4, n5
        Ptr<Socket> socket = Socket::CreateSocket(nodes.Get(i), TypeId::LookupByName("ns3::TcpSocketFactory"));
        socket->Bind(InetSocketAddress(Ipv4Address::GetAny(), port));
        socket->Listen();
        socket->SetAcceptCallback(MakeNullCallback<bool, Ptr<Socket>, const Address &>(), 
                                  MakeCallback(&AcceptCallback));
    }

    // 启用pcap捕获
    pointToPoint.EnablePcap("/home/seanet/zhangty/ns-3-allinone/ns-3-dev/pcap_files/n30-capture", d01.Get(1), true); // n0 到 n3
    pointToPoint.EnablePcap("/home/seanet/zhangty/ns-3-allinone/ns-3-dev/pcap_files/n32-capture", d32.Get(1), true);
    pointToPoint.EnablePcap("/home/seanet/zhangty/ns-3-allinone/ns-3-dev/pcap_files/n34-capture", d34.Get(1), true);
    pointToPoint.EnablePcap("/home/seanet/zhangty/ns-3-allinone/ns-3-dev/pcap_files/n35-capture", d35.Get(1), true);
    pointToPoint.EnablePcap("/home/seanet/zhangty/ns-3-allinone/ns-3-dev/pcap_files/n52-capture", d25.Get(1), true);
    pointToPoint.EnablePcap("/home/seanet/zhangty/ns-3-allinone/ns-3-dev/pcap_files/n54-capture", d45.Get(1), true);
    pointToPoint.EnablePcap("/home/seanet/zhangty/ns-3-allinone/ns-3-dev/pcap_files/n51-capture", d51.Get(1), true);

    Simulator::Stop(Seconds(20.0)); // 延长模拟时间
    Simulator::Run();
    Simulator::Destroy();
    return 0;
}

// 连接成功回调
void ConnectionSucceeded(Ptr<Socket> socket)
{
    NS_LOG_UNCOND(Simulator::Now().GetSeconds() << " Connection succeeded");
    
    auto it = g_pendingPackets.find(socket);
    if (it != g_pendingPackets.end()) {
        Ptr<Packet> packet = it->second;
        int bytesSent = socket->Send(packet);
        NS_LOG_UNCOND("Sent " << bytesSent << " bytes after connection established");
        g_pendingPackets.erase(it);
    }
}

// 连接失败回调
void ConnectionFailed(Ptr<Socket> socket)
{
    NS_LOG_UNCOND(Simulator::Now().GetSeconds() << " Connection failed");
    g_pendingPackets.erase(socket);
}

void AcceptCallback(Ptr<Socket> socket, const Address& from)
{
    NS_LOG_UNCOND(Simulator::Now().GetSeconds() << " Accepted connection from " << 
                  InetSocketAddress::ConvertFrom(from).GetIpv4());
    if (socket->GetNode()->GetId() == 1) { // 服务器n1
        socket->SetRecvCallback(MakeCallback(&ServerReceive));
    } else { // 中间节点
        socket->SetRecvCallback(MakeCallback(&DataReceived));
    }
}

void DataReceived(Ptr<Socket> socket)
{
    Ptr<Packet> packet;
    Address from;
    
    while ((packet = socket->RecvFrom(from)))
    {
        Ptr<Node> node = socket->GetNode();
        uint32_t nodeId = node->GetId();
        NS_LOG_UNCOND(Simulator::Now().GetSeconds() << " Node " << nodeId << " received " << packet->GetSize() << " bytes");

        if (nodeId == 3) { // n3节点
            Ptr<UniformRandomVariable> rand = CreateObject<UniformRandomVariable>();
            double prob = rand->GetValue();
            
            Ipv4Address nextHop;
            if (prob < 0.33) {
                nextHop = "10.1.2.2"; // n2
            } else if (prob < 0.66) {
                nextHop = "10.1.3.2"; // n4
            } else {
                nextHop = "10.1.4.2"; // n5
            }
            ForwardToNextHop(socket, packet->Copy(), nextHop);
        }
        else if (nodeId == 2) { // n2
            ForwardToNextHop(socket, packet->Copy(), "10.1.5.2"); // n5 via d25
        }
        else if (nodeId == 4) { // n4
            ForwardToNextHop(socket, packet->Copy(), "10.1.6.2"); // n5 via d45
        }
        else if (nodeId == 5) { // n5
            ForwardToNextHop(socket, packet->Copy(), "10.1.7.2"); // 服务器n1
        }
    }
}

void ForwardToNextHop(Ptr<Socket> incomingSocket, Ptr<Packet> packet, Ipv4Address nextHop)
{
    uint16_t port = 5000;
    Ptr<Node> node = incomingSocket->GetNode();
    
    Ptr<Socket> forwardSocket = Socket::CreateSocket(node, TypeId::LookupByName("ns3::TcpSocketFactory"));
    
    forwardSocket->SetConnectCallback(
        MakeCallback(&ConnectionSucceeded),
        MakeCallback(&ConnectionFailed));
    
    forwardSocket->SetSendCallback(MakeCallback(&DataSent));
    
    g_pendingPackets[forwardSocket] = packet;
    
    NS_LOG_UNCOND(Simulator::Now().GetSeconds() << " Connecting to " << nextHop);
    forwardSocket->Connect(InetSocketAddress(nextHop, port));
}

void DataSent(Ptr<Socket> socket, uint32_t available)
{
    NS_LOG_UNCOND(Simulator::Now().GetSeconds() << " Data sent, available: " << available);
}

void ServerReceive(Ptr<Socket> socket)
{
    Ptr<Packet> packet;
    Address from;
    
    while ((packet = socket->RecvFrom(from)))
    {
        NS_LOG_UNCOND(Simulator::Now().GetSeconds() << " Server received " << 
                      packet->GetSize() << " bytes");
    }
}