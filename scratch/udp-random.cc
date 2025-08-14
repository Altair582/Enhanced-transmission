#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/simulator.h" // Include for ns3::Time
#include "ns3/nstime.h"    // Include for ns3::Time arithmetic
#include "ns3/log.h"       // Include for NS_LOG_COMPONENT_DEFINE
#include "ns3/nstime.h"    // Include for ns3::Time arithmetic
#include "ns3/attribute.h" // Include for ns3::StringValue and other attribute-related classes
#include "ns3/string.h"    // Include for ns3::StringValue

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("UdpRandom"); // 日志组件定义

void CustomForwarding(Ptr<Socket> socket);
void ForwardToServer(Ptr<Socket> socket);
void ForwardToN5(Ptr<Socket> socket); 

Time g_sessionStartTime; // 会话开始时间
Time g_sessionEndTime;   // 会话结束时间;

int main(int argc, char *argv[])
{
    LogComponentEnable("UdpRandom", LOG_INFO);
    NodeContainer nodes;
    nodes.Create(6); // n0: 客户端, n1: 服务器, n2-n5: 中间节点

    // 创建点对点链路
    PointToPointHelper pointToPoint;
    pointToPoint.SetDeviceAttribute("DataRate", StringValue("10Mbps"));
    pointToPoint.SetChannelAttribute("Delay", StringValue("1ms"));

    // 创建点对点链路
    NetDeviceContainer devices1 = pointToPoint.Install(NodeContainer(nodes.Get(0), nodes.Get(3))); // n0 到 n3
    NetDeviceContainer devices2 = pointToPoint.Install(NodeContainer(nodes.Get(3), nodes.Get(2))); // n3 到 n2
    NetDeviceContainer devices3 = pointToPoint.Install(NodeContainer(nodes.Get(3), nodes.Get(4))); // n3 到 n4
    NetDeviceContainer devices4 = pointToPoint.Install(NodeContainer(nodes.Get(3), nodes.Get(5))); // n3 到 n5
    NetDeviceContainer devices5 = pointToPoint.Install(NodeContainer(nodes.Get(2), nodes.Get(5))); // n2 到 n5
    NetDeviceContainer devices6 = pointToPoint.Install(NodeContainer(nodes.Get(4), nodes.Get(5))); // n4 到 n5
    NetDeviceContainer devices7 = pointToPoint.Install(NodeContainer(nodes.Get(5), nodes.Get(1))); // n5 到 n1

    InternetStackHelper stack; // 创建 Internet 协议栈
    stack.Install(nodes);      // 安装协议栈到所有节点

    // 分配 IP 地址
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
    Ipv4InterfaceContainer interfaces7 = address.Assign(devices7);

    Ipv4GlobalRoutingHelper::PopulateRoutingTables();
    // 打印路由表
    //Ipv4GlobalRoutingHelper routingHelper;
    //routingHelper.PrintRoutingTableAllAt(ns3::Seconds(10.0), Create<OutputStreamWrapper>(&std::cout));

    uint16_t port = 9;// 端口号
    Address serverAddress(InetSocketAddress(interfaces1.GetAddress(1), port)); // 目标地址为 n3
    NS_LOG_UNCOND("n3Socket bound to port: " << port);// 绑定端口号
    PacketSinkHelper packetSinkHelper("ns3::UdpSocketFactory", serverAddress);// 创建 PacketSinkHelper
    ApplicationContainer serverApps = packetSinkHelper.Install(nodes.Get(1));//  // 安装 PacketSink 应用程序
    serverApps.Start(ns3::Seconds(1.0)); // 启动时间
    serverApps.Stop(Seconds(10.0)); // 停止时间

    // 设置客户端
    OnOffHelper onOffHelper("ns3::UdpSocketFactory", serverAddress);// 创建 OnOffHelper
    onOffHelper.SetAttribute("DataRate", StringValue("5Mbps"));// 设置数据速率
    onOffHelper.SetAttribute("PacketSize", UintegerValue(1024));// 设置数据包大小
    ApplicationContainer clientApps = onOffHelper.Install(nodes.Get(0));// 安装 OnOff 应用程序
    NS_LOG_UNCOND("Client started sending data to server.");
    clientApps.Start(Seconds(2.0));// 启动时间
    clientApps.Stop(Seconds(10.0)); // 停止时间

    // 自定义转发逻辑
    Ptr<Socket> n3Socket = Socket::CreateSocket(nodes.Get(3), TypeId::LookupByName("ns3::UdpSocketFactory"));// 创建套接字
    n3Socket->Bind(InetSocketAddress(Ipv4Address::GetAny(), port));// 绑定端口号
    Address localAddress;// 获取本地地址
    n3Socket->GetSockName(localAddress);// 获取本地地址
    InetSocketAddress inetLocalAddress = InetSocketAddress::ConvertFrom(localAddress);// 转换为 InetSocketAddress
    NS_LOG_UNCOND("n3Socket bound to address: " << inetLocalAddress.GetIpv4() << ", port: " << inetLocalAddress.GetPort());// 打印绑定的地址和端口号
    n3Socket->SetRecvCallback(MakeCallback(&CustomForwarding));// 设置接收回调函数

    // 在 n2 节点上创建套接字并设置回调
    Ptr<Socket> n2Socket = Socket::CreateSocket(nodes.Get(2), TypeId::LookupByName("ns3::UdpSocketFactory"));
    n2Socket->Bind(InetSocketAddress(Ipv4Address::GetAny(), port));
    n2Socket->SetRecvCallback(MakeCallback(&ForwardToN5));

    // 在 n4 节点上创建套接字并设置回调
    Ptr<Socket> n4Socket = Socket::CreateSocket(nodes.Get(4), TypeId::LookupByName("ns3::UdpSocketFactory"));
    n4Socket->Bind(InetSocketAddress(Ipv4Address::GetAny(), port));
    n4Socket->SetRecvCallback(MakeCallback(&ForwardToN5));

    Ptr<Socket> n5Socket = Socket::CreateSocket(nodes.Get(5), TypeId::LookupByName("ns3::UdpSocketFactory"));
    n5Socket->Bind(InetSocketAddress(Ipv4Address::GetAny(), port));
    n5Socket->GetSockName(localAddress);
    inetLocalAddress = InetSocketAddress::ConvertFrom(localAddress);
    NS_LOG_UNCOND("n5Socket bound to address: " << inetLocalAddress.GetIpv4() << ", port: " << inetLocalAddress.GetPort());
    n5Socket->SetRecvCallback(MakeCallback(&ForwardToServer));

    // 启用抓包功能
    pointToPoint.EnablePcap("n30-capture", devices1.Get(1), true); // n0 到 n3,
    pointToPoint.EnablePcap("n32-capture", devices2.Get(1), true);
    pointToPoint.EnablePcap("n34-capture", devices3.Get(1), true);
    pointToPoint.EnablePcap("n35-capture", devices4.Get(1), true);
    pointToPoint.EnablePcap("n52-capture", devices5.Get(1), true);
    pointToPoint.EnablePcap("n54-capture", devices6.Get(1), true);
    pointToPoint.EnablePcap("n51-capture", devices7.Get(1), true);

    Simulator::Run();
    NS_LOG_INFO("Simulation start.");
    // 输出会话完成时间
    if (!g_sessionStartTime.IsZero() && !g_sessionEndTime.IsZero())
    {
        double sessionDuration = g_sessionEndTime.GetSeconds() - g_sessionStartTime.GetSeconds();
        NS_LOG_UNCOND("Session duration: " << sessionDuration << " seconds");
    }
    else
    {
        NS_LOG_UNCOND("Session did not complete.");
    }

    Simulator::Destroy();

    return 0;
}

void CustomForwarding(Ptr<Socket> socket)
{
    NS_LOG_UNCOND("CustomForwarding function called."); // 确认函数被调用
    Ptr<Packet> packet;
    Address from;

    while ((packet = socket->RecvFrom(from)))
    {
        Ptr<UniformRandomVariable> randomVar = CreateObject<UniformRandomVariable>();
        double probability = randomVar->GetValue(); // 生成一个 0 到 1 之间的随机数
        NS_LOG_INFO("n3 收到数据包，生成的随机数: " << probability);

        if (probability < 0.33)
        {
            NS_LOG_INFO("n3 转发数据包到 n2");
            socket->SendTo(packet, 0, InetSocketAddress(Ipv4Address("10.1.2.2"), 9));
        }
        else if (probability < 0.66)
        {
            NS_LOG_INFO("n3 转发数据包到 n4");
            socket->SendTo(packet, 0, InetSocketAddress(Ipv4Address("10.1.3.2"), 9));
        }
        else
        {
            NS_LOG_INFO("n3 转发数据包到 n5");
            socket->SendTo(packet, 0, InetSocketAddress(Ipv4Address("10.1.4.2"), 9));
        }
    }
}

void ForwardToServer(Ptr<Socket> socket)
{
    NS_LOG_UNCOND("ForwardToServer function called."); // 确认函数被调用
    if (g_sessionStartTime.IsZero())
    {
        g_sessionStartTime = Simulator::Now();
        NS_LOG_INFO("Session start time recorded: " << g_sessionStartTime.GetSeconds());
    }
    g_sessionEndTime = Simulator::Now();
    NS_LOG_INFO("Session end time updated: " << g_sessionEndTime.GetSeconds());

    Ptr<Packet> packet;
    Address from;

    while ((packet = socket->RecvFrom(from)))
    {
        NS_LOG_INFO("n5 转发数据包到服务器 n1");
        socket->SendTo(packet, 0, InetSocketAddress(Ipv4Address("10.1.7.2"), 9));
    }
}
void ForwardToN5(Ptr<Socket> socket)
{
    NS_LOG_UNCOND("ForwardToN5 function called."); // 确认函数被调用
    Ptr<Packet> packet;
    Address from;

    while ((packet = socket->RecvFrom(from)))
    {
        NS_LOG_INFO("转发数据包到 n5");
        socket->SendTo(packet, 0, InetSocketAddress(Ipv4Address("10.1.5.2"), 9)); // 转发到 n5
    }
}