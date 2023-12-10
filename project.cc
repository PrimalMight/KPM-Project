#include "ns3/applications-module.h"
#include "ns3/config-store-module.h"
#include "ns3/core-module.h"
#include "ns3/data-rate.h"
#include "ns3/epc-helper.h"
#include "ns3/flow-monitor-helper.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/gnuplot.h"
#include "ns3/internet-apps-module.h"
#include "ns3/internet-module.h"
#include "ns3/ipv4-global-routing-helper.h"
#include "ns3/lte-helper.h"
#include "ns3/lte-module.h"
#include "ns3/mobility-module.h"
#include "ns3/netanim-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-helper.h"
#include "ns3/traffic-control-module.h"

#include <fstream>
#include <string>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("lte-full");

int
main(int argc, char* argv[])
{
    uint16_t numberOfUes = 15;
    uint16_t numberOf_eNodeBs = 3;
    uint16_t dlBandwidth = 75;
    uint16_t upBandwidth = 75;
    uint16_t videoPacketSize = 1500;
    uint16_t ftpPacketSize = 200;
    uint32_t ftpDataSize = 10000000;
    uint32_t videoDataSize = 1000000;
    double simTime = 60.0;
    double interval = 20.0; // ms
    double distance = 300.0;
    double txPower = 10;
    double walkSpeed = 2.0;
    bool useCa = true;

    //variables used in simulation for cmd args
    CommandLine cmd;
    cmd.AddValue("simTime", "Total duration of the simulation [s])", simTime);
    cmd.AddValue("distance", "Distance between eNBs [m]", distance);
    cmd.AddValue("useCa", "Whether to use carrier aggregation.", useCa);
    cmd.AddValue("interval", "Inter-packet interval for UDP client [ms]", interval);
    cmd.AddValue("dlBandwidth", "Downlink bandwidth of eNBs", dlBandwidth);
    cmd.AddValue("upBandwidth", "Uplink bandwidth of eNBs", upBandwidth);
    cmd.AddValue("txPower", "Transmission power of UEs", txPower);
    cmd.AddValue("ftpPacketSize", "Size of FTP packets to sent", ftpPacketSize);
    cmd.AddValue("ftpDataSize", "The amount of data to be sent through FTP", ftpDataSize);
    cmd.AddValue("videoPacketSize", "Size of video packets to be sent by the remote server", videoPacketSize);
    cmd.AddValue("videoDataSize", "The amount of video data to be sent", videoDataSize);
    cmd.AddValue("walkSpeed", "The speed of pedestrians default=2.0", walkSpeed);
    cmd.Parse(argc, argv);

    if (useCa)
    {
        Config::SetDefault("ns3::LteHelper::UseCa", BooleanValue(useCa));
        Config::SetDefault("ns3::LteHelper::NumberOfComponentCarriers", UintegerValue(2));
        Config::SetDefault("ns3::LteHelper::EnbComponentCarrierManager",
                           StringValue("ns3::RrComponentCarrierManager"));
    }
    ConfigStore inputConfig;
    inputConfig.ConfigureDefaults();
    cmd.Parse(argc, argv);

    Ptr<LteHelper> lteHelper = CreateObject<LteHelper>(); // create LteHelper object
    Ptr<PointToPointEpcHelper> epcHelper =
        CreateObject<PointToPointEpcHelper>(); // PointToPointEpcHelper
    lteHelper->SetEpcHelper(epcHelper);        // enable the use of EPC by LTE helper

    lteHelper->SetEnbDeviceAttribute("DlBandwidth", UintegerValue(dlBandwidth));
    lteHelper->SetEnbDeviceAttribute("UlBandwidth", UintegerValue(upBandwidth));

    LogComponentEnable("Ping", LOG_LEVEL_ALL);

    Ptr<Node> pgw =
        epcHelper->GetPgwNode(); // get the PGW node
    Ptr<Node> sgw = epcHelper->GetSgwNode();
    
    // Get the MME node by iterating over all nodes in the simulation
    Ptr<Node> mme = 0; // Initialize to nullptr
    for (NodeList::Iterator it = NodeList::Begin(); it != NodeList::End(); ++it)
    {
        Ptr<Node> node = *it;
        if ((*it)->GetId() == 2)
        {
            // Node with ID 2 (mme) found
            mme = node;
            break;
        }
    }

    // Create a single RemoteHost
    NodeContainer remoteHostContainer; // container for remote node
    remoteHostContainer.Create(1);     // create 1 node
    Ptr<Node> remoteHost = remoteHostContainer.Get(0);
    InternetStackHelper internet;
    internet.Install(remoteHostContainer); // aggregate stack implementations (ipv4, ipv6, udp, tcp)
                                           // to remote node

    // Create the Internet
    PointToPointHelper p2ph;
    p2ph.SetDeviceAttribute("DataRate", DataRateValue(DataRate("100Gb/s"))); // p2p data rate
    p2ph.SetDeviceAttribute("Mtu", UintegerValue(1500));                     // p2p mtu
    p2ph.SetChannelAttribute("Delay", TimeValue(Seconds(0.010)));            // p2p delay
    NetDeviceContainer internetDevices = p2ph.Install(pgw, remoteHost);
    Ipv4AddressHelper ipv4h;
    ipv4h.SetBase("1.0.0.0",
                  "255.0.0.0"); // allocates IP addresses (network number and  mask), p2p interface
    Ipv4InterfaceContainer internetIpIfaces =
        ipv4h.Assign(internetDevices); // assign IPs to each p2p device
    Ipv4Address remoteHostAddr = internetIpIfaces.GetAddress(1);

    // TODO: Is this ok?
    Ipv4StaticRoutingHelper ipv4RoutingHelper;
    Ptr<Ipv4StaticRouting> remoteHostStaticRouting =
        ipv4RoutingHelper.GetStaticRouting(remoteHost->GetObject<Ipv4>());
    remoteHostStaticRouting->AddNetworkRouteTo(Ipv4Address("7.0.0.0"), Ipv4Mask("255.0.0.0"), 1);

    NodeContainer ueNodes;
    NodeContainer enbNodes;
    enbNodes.Create(numberOf_eNodeBs); // create eNB nodes
    ueNodes.Create(numberOfUes);       // create UE nodes

    // ------ Install Mobility Model --------
    Ptr<ListPositionAllocator> positionAllocEnb = CreateObject<ListPositionAllocator>();
    for (uint16_t i = 0; i < numberOf_eNodeBs; i++)
    {
        positionAllocEnb->Add(Vector(200 + distance * i, 700, 0));
    }

    MobilityHelper mobility;
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.SetPositionAllocator(positionAllocEnb);
    mobility.Install(enbNodes);

    mobility.Install(remoteHost);
    Ptr<ConstantPositionMobilityModel> remote_mob =
        remoteHostContainer.Get(0)->GetObject<ConstantPositionMobilityModel>();
    remote_mob->SetPosition(Vector(300.0, 300.0, 0));

    mobility.Install(pgw);
    Ptr<ConstantPositionMobilityModel> pgwMobility =
        pgw->GetObject<ConstantPositionMobilityModel>();
    pgwMobility->SetPosition(Vector(400.0, 400.0, 0));

    mobility.Install(sgw);
    Ptr<ConstantPositionMobilityModel> sgwMobility =
        sgw->GetObject<ConstantPositionMobilityModel>();
    sgwMobility->SetPosition(Vector(500.0, 500.0, 0));

    mobility.Install(mme);
    Ptr<ConstantPositionMobilityModel> mmeMobility =
        mme->GetObject<ConstantPositionMobilityModel>();
    mmeMobility->SetPosition(Vector(600.0, 400.0, 0));

    Ptr<ListPositionAllocator> positionAllocUe = CreateObject<ListPositionAllocator>();
    uint16_t it = 0;
    for (uint16_t i = 0; i < numberOfUes; i++)
    {
        if (it == 0)
        {
            double x = 200 + 20.0 * std::cos(2 * M_PI * i / numberOfUes);
            double y = 700 + 20.0 * std::sin(2 * M_PI * i / numberOfUes);
            positionAllocUe->Add(Vector(x, y, 0));
            it += 1;
        }
        else if (it == 1)
        {
            double x = 500 + 20.0 * std::cos(2 * M_PI * i / numberOfUes);
            double y = 700 + 20.0 * std::sin(2 * M_PI * i / numberOfUes);
            positionAllocUe->Add(Vector(x, y, 0));
            it += 1;
        }
        else
        {
            double x = 800 + 20.0 * std::cos(2 * M_PI * i / numberOfUes);
            double y = 700 + 20.0 * std::sin(2 * M_PI * i / numberOfUes);
            positionAllocUe->Add(Vector(x, y, 0));
            it = 0;
        }
    }
    // Then make UEs move

    mobility.SetMobilityModel("ns3::RandomWalk2dMobilityModel",
                              "Mode",
                              StringValue("Time"),
                              "Time",
                              StringValue(std::to_string(simTime) + "s"),
                              "Speed",
                              StringValue("ns3::ConstantRandomVariable[Constant="+std::to_string(walkSpeed)+"]"),
                              "Bounds",
                              StringValue("150|850|150|850"));

    mobility.SetPositionAllocator(positionAllocUe);

    mobility.Install(ueNodes);
    
    }
    // ------ END Install Mobility Model --------

    // Install LTE Devices to the nodes
    NetDeviceContainer enbLteDevs =
        lteHelper->InstallEnbDevice(enbNodes); // add eNB nodes to the container
    NetDeviceContainer ueLteDevs =
        lteHelper->InstallUeDevice(ueNodes); // add UE nodes to the container

    // SHOW STATS OF eNodeB's
    for (uint8_t i = 0; i < ueLteDevs.GetN(); i++)
    {
        Ptr<NetDevice> ueNetDev = ueLteDevs.Get(i);
        Ptr<LteUeNetDevice> ueLteNetDev = DynamicCast<LteUeNetDevice>(ueNetDev);
        Ptr<LteUePhy> uePhy = ueLteNetDev->GetPhy();
        uePhy->SetTxPower(txPower);
    }
    
    for (uint16_t i = 0; i < numberOf_eNodeBs; i++)
    {
        Ptr<NetDevice> enbNetDev = enbLteDevs.Get(i);
        Ptr<LteEnbNetDevice> enbLteNetDev = DynamicCast<LteEnbNetDevice>(enbNetDev);
        uint32_t dlEarfcn = enbLteNetDev->GetDlEarfcn();
        uint32_t ulEarfcn = enbLteNetDev->GetUlEarfcn();
        uint16_t dl_bwd = enbLteNetDev->GetDlBandwidth();
        uint16_t ul_bwd = enbLteNetDev->GetUlBandwidth();
        std::cout << "eNode " << i << " Stats:" << std::endl;
        std::cout << "Downlink BW: " << dl_bwd << std::endl;
        std::cout << "Uplink BW: " << ul_bwd << std::endl;
        std::cout << "Downlink Earfcn: " << dlEarfcn << std::endl;
        std::cout << "Uplink Earfcn: " << ulEarfcn << std::endl;

        Ptr<NetDevice> ueNetDev = ueLteDevs.Get(i);
        Ptr<LteUeNetDevice> ueLteNetDev = DynamicCast<LteUeNetDevice>(ueNetDev);
        Ptr<LteUePhy> uePhy = ueLteNetDev->GetPhy();
        double txPowerUe = uePhy->GetTxPower();
        std::cout << "TxPower UE: " << txPowerUe << std::endl;
        std::cout << "---------------------------" << std::endl;
    }

    

    // Install the IP stack on the UEs
    internet.Install(ueNodes);
    // Assign IP address to UEs
    Ipv4InterfaceContainer ueIpIface;
    ueIpIface = epcHelper->AssignUeIpv4Address(NetDeviceContainer(ueLteDevs));
    for (uint32_t u = 0; u < ueNodes.GetN(); ++u)
    {
        Ptr<Node> ueNode = ueNodes.Get(u);
        // Set the default gateway for the UE
        Ptr<Ipv4StaticRouting> ueStaticRouting =
            ipv4RoutingHelper.GetStaticRouting(ueNode->GetObject<Ipv4>());
        ueStaticRouting->SetDefaultRoute(epcHelper->GetUeDefaultGatewayAddress(),
                                         1); // default route
    }

    // Attach UEs to eNodeBs
    lteHelper->Attach(ueLteDevs);

    // ---------- IMPLEMENT STREAMING FLOW ----------
    // Define the port for video streaming
    uint16_t videoPort1 = 100;

    // Create and install UDP Clients to UEs
    PacketSinkHelper udpSinkHelper("ns3::UdpSocketFactory",
                                   InetSocketAddress(Ipv4Address::GetAny(), videoPort1));
    ApplicationContainer udpSink;

    for (uint8_t i = 0; i < 3; i++)
    {
        udpSink.Add(udpSinkHelper.Install(ueNodes.Get(i)));
    }

    udpSink.Start(Seconds(2.0));
    udpSink.Stop(Seconds(simTime));
    // Create and install separate UDP servers for each client
    UdpClientHelper firstVideoServer(ueIpIface.GetAddress(0), videoPort1);
    firstVideoServer.SetAttribute("MaxPackets", UintegerValue(videoDataSize));
    firstVideoServer.SetAttribute("Interval", TimeValue(MilliSeconds(interval)));
    firstVideoServer.SetAttribute("PacketSize", UintegerValue(videoPacketSize));
    ApplicationContainer firstVideo = firstVideoServer.Install(remoteHost);
    firstVideo.Start(Seconds(2.0));
    firstVideo.Stop(Seconds(simTime));

    UdpClientHelper secondVideoServer(ueIpIface.GetAddress(1), videoPort1);
    secondVideoServer.SetAttribute("MaxPackets", UintegerValue(videoDataSize));
    secondVideoServer.SetAttribute("Interval", TimeValue(MilliSeconds(interval)));
    secondVideoServer.SetAttribute("PacketSize", UintegerValue(videoPacketSize));
    ApplicationContainer secondVideo = secondVideoServer.Install(remoteHost);
    secondVideo.Start(Seconds(2.0));
    secondVideo.Stop(Seconds(simTime));

    UdpClientHelper thirdVideoServer(ueIpIface.GetAddress(2), videoPort1);
    thirdVideoServer.SetAttribute("MaxPackets", UintegerValue(videoDataSize));
    thirdVideoServer.SetAttribute("Interval", TimeValue(MilliSeconds(interval)));
    thirdVideoServer.SetAttribute("PacketSize", UintegerValue(videoPacketSize));
    ApplicationContainer thirdVideo = thirdVideoServer.Install(remoteHost);
    thirdVideo.Start(Seconds(2.0));
    thirdVideo.Stop(Seconds(simTime));

    // ---------- END IMPLEMENT STREAMING FLOW ----------

    // ---------- IMPLEMENT FTP FLOW ----------
    uint16_t firstUeID = 4;  // Choose the UE that will act as the FTP server
    uint16_t secondUeID = 8; // Choose the UE that will act as the FTP client

    // Define the port for FTP server
    uint16_t ftpPort = 21;

    // Install the BulkSend application on the UE acting as the FTP server
    Ipv4Address secondUe = ueIpIface.GetAddress(secondUeID);

    BulkSendHelper ftpFirstServerHelper("ns3::TcpSocketFactory",
                                        InetSocketAddress(secondUe, ftpPort));
    ftpFirstServerHelper.SetAttribute("MaxBytes", UintegerValue(ftpDataSize));
    ftpFirstServerHelper.SetAttribute("SendSize", UintegerValue(ftpPacketSize));
    ApplicationContainer ftpFirstServer = ftpFirstServerHelper.Install(ueNodes.Get(firstUeID));
    ftpFirstServer.Start(Seconds(2.0));
    ftpFirstServer.Stop(Seconds(simTime));

    //  Install PacketSink on the UE acting as the FTP client
    PacketSinkHelper ftpSecondClientHelper("ns3::TcpSocketFactory",
                                           InetSocketAddress(Ipv4Address::GetAny(), ftpPort));
    ApplicationContainer ftpSecondClient = ftpSecondClientHelper.Install(ueNodes.Get(secondUeID));
    ftpSecondClient.Start(Seconds(2.0));
    ftpSecondClient.Stop(Seconds(simTime));

    // ---------- END IMPLEMENT FTP FLOW ----------

    // Uncomment to enable traces
    // lteHelper->EnableTraces();

    // Animation definition
    AnimationInterface anim("project.xml");

    /// Optional step
    anim.SetMobilityPollInterval(Seconds(0.25));

    // Uncomment to enable recording of packet Metadata
    // anim.EnablePacketMetadata(true);

    unsigned long long maxAnimPackets = 0xFFFFFFFFFFFFFFFF;
    anim.SetMaxPktsPerTraceFile(maxAnimPackets);

    anim.UpdateNodeDescription(pgw, "PGW");
    anim.UpdateNodeDescription(remoteHost, "RemoteHost");
    anim.UpdateNodeDescription(1, "SGW");
    anim.UpdateNodeDescription(2, "MME");

    for (uint32_t u = 0; u < ueNodes.GetN(); ++u)
    {
        anim.UpdateNodeDescription(ueNodes.Get(u), "Ue_" + std::to_string(u));
        anim.UpdateNodeColor(ueNodes.Get(u), 0, 0, 255); // Optional
    }

    for (uint32_t u = 0; u < enbNodes.GetN(); ++u)
    {
        anim.UpdateNodeDescription(enbNodes.Get(u), "eNodeB_" + std::to_string(u));
        anim.UpdateNodeColor(enbNodes.Get(u), 0, 255, 0); // Optional
    }

    // Uncomment to enable PCAP tracing
    p2ph.EnablePcapAll("project-pcap");

    Ptr<FlowMonitor> monitor; // = flowMonHelper.InstallAll();
    FlowMonitorHelper flowMonHelper;
    monitor = flowMonHelper.Install(enbNodes);
    monitor = flowMonHelper.Install(ueNodes);
    monitor = flowMonHelper.Install(remoteHost);

    Simulator::Stop(Seconds(simTime));
    Simulator::Run();

    monitor->CheckForLostPackets();
    Ptr<Ipv4FlowClassifier> classifier =
        DynamicCast<Ipv4FlowClassifier>(flowMonHelper.GetClassifier());
    std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats();

    monitor->SerializeToXmlFile("lte-full.flowmon", true, true);

    std::cout << std::endl << "*** Flow monitor statistic ***" << std::endl;
    for (std::map<FlowId, FlowMonitor::FlowStats>::const_iterator i = stats.begin();
         i != stats.end();
         ++i)
    {
        // if (i-> first > 2) {
        double Delay, DataRate;
        Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(i->first);
        std::cout << "Flow ID: " << i->first << std::endl;
        std::cout << "Src add: " << t.sourceAddress << "-> Dst add: " << t.destinationAddress
                  << std::endl;
        std::cout << "Src port: " << t.sourcePort << "-> Dst port: " << t.destinationPort
                  << std::endl;
        std::cout << "Tx Packets/Bytes: " << i->second.txPackets << "/" << i->second.txBytes
                  << std::endl;
        std::cout << "Rx Packets/Bytes: " << i->second.rxPackets << "/" << i->second.rxBytes
                  << std::endl;
        std::cout << "Throughput: "
                  << i->second.rxBytes * 8.0 /
                         (i->second.timeLastRxPacket.GetSeconds() -
                          i->second.timeFirstTxPacket.GetSeconds()) /
                         1024
                  << "kb/s" << std::endl;
        std::cout << "Delay sum: " << i->second.delaySum.GetMilliSeconds() << "ms" << std::endl;
        std::cout << "Mean delay: "
                  << (i->second.delaySum.GetSeconds() / i->second.rxPackets) * 1000 << "ms"
                  << std::endl;

        // gnuplot Delay
        Delay = (i->second.delaySum.GetSeconds() / i->second.rxPackets) * 1000;
        DataRate =
            i->second.rxBytes * 8.0 /
            (i->second.timeLastRxPacket.GetSeconds() - i->second.timeFirstTxPacket.GetSeconds()) /
            1024;

        std::cout << "Jitter sum: " << i->second.jitterSum.GetMilliSeconds() << "ms" << std::endl;
        std::cout << "Mean jitter: "
                  << (i->second.jitterSum.GetSeconds() / (i->second.rxPackets - 1)) * 1000 << "ms"
                  << std::endl;
        // std::cout << "Lost Packets: " << i->second.lostPackets << std::endl;
        std::cout << "Lost Packets: " << i->second.txPackets - i->second.rxPackets << std::endl;
        std::cout << "Packet loss: "
                  << (((i->second.txPackets - i->second.rxPackets) * 1.0) / i->second.txPackets) *
                         100
                  << "%" << std::endl;
        std::cout << "------------------------------------------------" << std::endl;
    }


    Simulator::Destroy();
    return 0;
}
