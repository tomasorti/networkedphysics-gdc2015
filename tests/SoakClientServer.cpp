#include "Connection.h"
#include "TestMessages.h"
#include "TestPackets.h"
#include "TestClientServer.h"
#include "TestChannelStructure.h"
#include "ReliableMessageChannel.h"
#include "NetworkSimulator.h"
#include "BSDSocket.h"
#include "Network.h"
#include <time.h>

using namespace protocol;

const int NumServers = 8;
const int NumClients = 256;
const int NumClientsPerServer = 16;
const int BaseServerPort = 10000;
const int BaseClientPort = 20000;

struct ClientInfo
{
    Address address;
    TestClient * client;
    Block * clientData;
    NetworkInterface * networkInterface;
    NetworkSimulator * networkSimulator;
    int state;
    int serverIndex;
    double lastReceiveTime;
    uint16_t sendSequence;
    uint16_t receiveSequence;

    ClientInfo()
    {
        Clear();
    }

    void Clear()
    {
        state = 0;
        serverIndex = -1;
        lastReceiveTime = 0.0;
        sendSequence = 0;
        receiveSequence = 0;
    }
};

struct ServerInfo
{
    Address address;
    TestServer * server;
    Block * serverData;
    NetworkInterface * networkInterface;
    NetworkSimulator * networkSimulator;
};

void soak_test()
{
#if PROFILE
    printf( "[profile client server]\n" );
#else
    printf( "[soak client server]\n" );
#endif

    TestMessageFactory messageFactory( memory::default_allocator() );

    TestChannelStructure channelStructure( messageFactory );

    TestPacketFactory packetFactory( memory::default_allocator() );

    // create a bunch of servers

    ServerInfo serverInfo[NumServers];

    for( int i = 0; i < NumServers; ++i )
    {
        serverInfo[i].address = Address( "::1" );
        serverInfo[i].address.SetPort( BaseServerPort + i );

        BSDSocketConfig bsdSocketConfig;
        bsdSocketConfig.port = BaseServerPort + i;
        bsdSocketConfig.maxPacketSize = 1200;
        bsdSocketConfig.packetFactory = &packetFactory;
        serverInfo[i].networkInterface = CORE_NEW( memory::default_allocator(), BSDSocket, bsdSocketConfig );

        NetworkSimulatorConfig networkSimulatorConfig;
        networkSimulatorConfig.packetFactory = &packetFactory;
        serverInfo[i].networkSimulator = CORE_NEW( memory::default_allocator(), NetworkSimulator, networkSimulatorConfig );
        serverInfo[i].networkSimulator->AddState( { 0.0f, 0.0f, 0.0f } );
        serverInfo[i].networkSimulator->AddState( { 0.1f, 0.1f, 5.0f } );
        serverInfo[i].networkSimulator->AddState( { 0.2f, 0.1f, 10.0f } );
        serverInfo[i].networkSimulator->AddState( { 0.25f, 0.1f, 25.0f } );

        const int serverDataSize = sizeof(TestContext) + 256 * i + 11 + i;
        serverInfo[i].serverData = CORE_NEW( memory::default_allocator(), Block, memory::default_allocator(), serverDataSize );
        {
            uint8_t * data = serverInfo[i].serverData->GetData();
            for ( int j = 0; j < serverDataSize; ++j )
                data[j] = ( 10 + i + j ) % 256;

            auto testContext = (TestContext*) data;
            testContext->value_min = -1 - ( rand() % 100000000 );
            testContext->value_max = rand() % 1000000000;
        }

        ServerConfig serverConfig;
        serverConfig.serverData = serverInfo[i].serverData;
        serverConfig.maxClients = NumClientsPerServer;
        serverConfig.channelStructure = &channelStructure;
        serverConfig.networkInterface = serverInfo[i].networkInterface;
        serverConfig.networkSimulator = serverInfo[i].networkSimulator;

        serverInfo[i].server = CORE_NEW( memory::default_allocator(), TestServer, serverConfig );
    }

    // create a bunch of clients

    ClientInfo clientInfo[NumClients];

    for ( int i = 0; i < NumClients; ++i )
    {
        clientInfo[i].address = Address( "::1" );
        clientInfo[i].address.SetPort( BaseClientPort + i );

        BSDSocketConfig bsdSocketConfig;
        bsdSocketConfig.port = BaseClientPort + i;
        bsdSocketConfig.maxPacketSize = 1200;
        bsdSocketConfig.packetFactory = &packetFactory;
        clientInfo[i].networkInterface = CORE_NEW( memory::default_allocator(), BSDSocket, bsdSocketConfig );

        NetworkSimulatorConfig networkSimulatorConfig;
        networkSimulatorConfig.packetFactory = &packetFactory;
        clientInfo[i].networkSimulator = CORE_NEW( memory::default_allocator(), NetworkSimulator, networkSimulatorConfig );
        clientInfo[i].networkSimulator->AddState( { 0.0f, 0.0f, 0.0f } );
        clientInfo[i].networkSimulator->AddState( { 0.1f, 0.1f, 5.0f } );
        clientInfo[i].networkSimulator->AddState( { 0.2f, 0.1f, 10.0f } );
        clientInfo[i].networkSimulator->AddState( { 0.25f, 0.1f, 25.0f } );

        const int clientDataSize = 10 + 64 * i + 21 + i;
        clientInfo[i].clientData = CORE_NEW( memory::default_allocator(), Block, memory::default_allocator(), clientDataSize );
        {
            uint8_t * data = clientInfo[i].clientData->GetData();
            for ( int j = 0; j < clientDataSize; ++j )
                data[j] = ( 20 + i + j ) % 256;
        }

        ClientConfig clientConfig;
        clientConfig.clientData = clientInfo[i].clientData;
        clientConfig.channelStructure = &channelStructure;        
        clientConfig.networkInterface = clientInfo[i].networkInterface;
        clientConfig.networkSimulator = clientInfo[i].networkSimulator;

        clientInfo[i].client = CORE_NEW( memory::default_allocator(), TestClient, clientConfig );
        clientInfo[i].Clear();
    }

    TimeBase timeBase;
    timeBase.deltaTime = 1.0 / 60.0;

    float lastConnectedClientTime = 0.0;

    while ( true )
    //for ( int i = 0; i < 10000; ++i )
    {
        for ( int i = 0; i < NumServers; ++i )
        {
            serverInfo[i].server->Update( timeBase );

            while ( true )
            {
                auto packet = clientInfo[i].networkSimulator->ReceivePacket();
                if ( !packet )
                    break;
                clientInfo[i].networkInterface->SendPacket( packet->GetAddress(), packet );
            }

            serverInfo[i].networkInterface->Update( timeBase );

            for ( int j = 0; j < NumClientsPerServer; ++j )
            {
                if ( serverInfo[i].server->GetClientState(j) == SERVER_CLIENT_STATE_CONNECTED )
                {
                    auto connection = serverInfo[i].server->GetClientConnection( j );
                    auto messageChannel = static_cast<ReliableMessageChannel*>( connection->GetChannel( 0 ) );
                    while ( true )
                    {
                        if ( !messageChannel->CanSendMessage() )
                            break;

                        auto message = messageChannel->ReceiveMessage();
                        if ( !message )
                            break;

                        PROTOCOL_CHECK( message->GetType() == MESSAGE_TEST_CONTEXT );

                        auto testContextMessage = (TestContextMessage*) message;

                        auto replyMessage = (TestContextMessage*) messageFactory.Create( MESSAGE_TEST_CONTEXT );
                        replyMessage->sequence = testContextMessage->sequence;
                        replyMessage->value = testContextMessage->value;
                        messageChannel->SendMessage( replyMessage );

                        messageFactory.Release( message );
                    }
                }
            }
        }

        for ( int i = 0; i < NumClients; ++i )
        {
            clientInfo[i].client->Update( timeBase );

            if ( clientInfo[i].client->HasError() )
            {
                printf( "%09.2f - client %d error: %s\n", timeBase.time, i, GetClientErrorString( clientInfo[i].client->GetError() ) );
                clientInfo[i].client->ClearError();
            }

            const int oldState = clientInfo[i].state;
            const int newState = clientInfo[i].state = clientInfo[i].client->GetState();
            if ( newState != oldState )
            {
                if ( newState == CLIENT_STATE_CONNECTED )
                {
                    const int j = clientInfo[i].serverIndex;

                    printf( "%09.2f - client %d successfully connected to server %d\n", timeBase.time, i, j );

                    PROTOCOL_CHECK( j >= 0 );
                    PROTOCOL_CHECK( j < NumServers );

                    int clientSlot = serverInfo[j].server->FindClientSlot( clientInfo[i].address, clientInfo[i].client->GetClientId(), clientInfo[i].client->GetServerId() );

                    PROTOCOL_CHECK( clientSlot != -1 );

                    // verify client data matches

                    const Block * serverClientData = serverInfo[j].server->GetClientData( clientSlot );
                    PROTOCOL_CHECK( serverClientData );
                    PROTOCOL_CHECK( serverClientData->GetSize() > 0 );
                    PROTOCOL_CHECK( serverClientData->GetSize() == clientInfo[i].clientData->GetSize() );
                    PROTOCOL_CHECK( memcmp( serverClientData->GetData(), clientInfo[i].clientData->GetData(), serverClientData->GetSize() ) == 0 );

                    // verify server data matches

                    const Block * clientServerData = clientInfo[i].client->GetServerData();
                    PROTOCOL_CHECK( clientServerData );
                    PROTOCOL_CHECK( clientServerData->GetSize() > 0 );
                    PROTOCOL_CHECK( clientServerData->GetSize() == serverInfo[j].serverData->GetSize() );
                    PROTOCOL_CHECK( memcmp( clientServerData->GetData(), serverInfo[j].serverData->GetData(), clientServerData->GetSize() ) == 0 );

                    clientInfo[i].lastReceiveTime = timeBase.time;
                }

                if ( newState == CLIENT_STATE_DISCONNECTED )
                {
                    if ( oldState != CLIENT_STATE_CONNECTED )
                        printf( "%09.2f - client %d failed to connect to server %d\n", timeBase.time, i, clientInfo[i].serverIndex );
                    else
                        printf( "%09.2f - client %d was disconnected from server %d\n", timeBase.time, i, clientInfo[i].serverIndex );

                    clientInfo[i].Clear();
                }
            }

            if ( clientInfo[i].client->IsConnected() )
            {
                auto testContext = clientInfo[i].client->GetTestContext();
                auto connection = clientInfo[i].client->GetConnection();
                auto messageChannel = static_cast<ReliableMessageChannel*>( connection->GetChannel( 0 ) );
                if ( messageChannel->CanSendMessage() )
                {
                    auto message = (TestContextMessage*) messageFactory.Create( MESSAGE_TEST_CONTEXT );
                    PROTOCOL_CHECK( message );
                    message->sequence = clientInfo[i].sendSequence++;
                    message->value = random_int( testContext->value_min, testContext->value_max );
                    messageChannel->SendMessage( message );
                }

                while ( true )
                {
                    auto message = messageChannel->ReceiveMessage();
                    if ( !message )
                        break;

                    PROTOCOL_CHECK( message->GetType() == MESSAGE_TEST_CONTEXT );

                    TestContextMessage * testContextMessage = (TestContextMessage*) message;

                    PROTOCOL_CHECK( testContextMessage->sequence == clientInfo[i].receiveSequence++ );

                    clientInfo[i].lastReceiveTime = timeBase.time;

                    messageFactory.Release( message );
                }

                PROTOCOL_CHECK( clientInfo[i].lastReceiveTime >= timeBase.time - 30.0 );
            }

            if ( clientInfo[i].client->IsConnected() )
            {
                while ( true )
                {
                    auto packet = clientInfo[i].networkSimulator->ReceivePacket();
                    if ( !packet )
                        break;
                    clientInfo[i].networkInterface->SendPacket( packet->GetAddress(), packet );
                }

                clientInfo[i].networkInterface->Update( timeBase );

                if ( clientInfo[i].receiveSequence > 100 && ( rand() % 100 ) == 0 )
                {
                    printf( "%09.2f - disconnect client %d from server %d\n", timeBase.time, i, clientInfo[i].serverIndex );
                    clientInfo[i].client->Disconnect();
                    clientInfo[i].Clear();
                }
            }

            if ( clientInfo[i].client->GetState() == CLIENT_STATE_DISCONNECTED )
            {
                if ( ( rand() % 200 ) == 0 )
                {
                    const int serverIndex = rand() % NumServers;
                    printf( "%09.2f - connect client %d to server %d\n", timeBase.time, i, serverIndex );
                    clientInfo[i].client->Connect( serverInfo[serverIndex].address ); 
                    clientInfo[i].serverIndex = serverIndex;                  
                }
            }
        }

        for ( int i = 0; i < NumClients; ++i )
        {
            if ( clientInfo[i].client->GetState() == CLIENT_STATE_CONNECTED )
            {
                lastConnectedClientTime = timeBase.time;
                break;                
            }
        }

        PROTOCOL_CHECK( lastConnectedClientTime >= timeBase.time - 10.0 );

        timeBase.time += timeBase.deltaTime;
    }

    for ( int i = 0; i < NumServers; ++i )
    {
        printf( "server %d:\n", i );
        for ( int j = 0; j < NumClientsPerServer; ++j )
        {
            printf( " - client slot %d: %s\n", j, GetServerClientStateName( serverInfo[i].server->GetClientState( j ) ) );
        }

        CORE_DELETE( memory::default_allocator(), TestServer, serverInfo[i].server );
        CORE_DELETE( memory::default_allocator(), Block, serverInfo[i].serverData );
        CORE_DELETE( memory::default_allocator(), NetworkInterface, serverInfo[i].networkInterface );
    }

    for ( int i = 0; i < NumClients; ++i )
    {
        printf( "client %d: %s\n", i, GetClientStateName( clientInfo[i].client->GetState() ) );

        CORE_DELETE( memory::default_allocator(), TestClient, clientInfo[i].client );
        CORE_DELETE( memory::default_allocator(), Block, clientInfo[i].clientData );
        CORE_DELETE( memory::default_allocator(), NetworkInterface, clientInfo[i].networkInterface );
    }
}

int main()
{
    srand( time( nullptr ) );

    memory::initialize();

    srand( time( nullptr ) );

    if ( !InitializeNetwork() )
    {
        printf( "failed to initialize network\n" );
        return 1;
    }

    CORE_ASSERT( IsNetworkInitialized() );

    soak_test();

    ShutdownNetwork();

    memory::shutdown();

    return 0;
}
