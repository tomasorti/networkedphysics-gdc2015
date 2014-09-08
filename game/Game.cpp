#include "Common.h"
#include "Network.h"
#include <time.h>
#include <stdlib.h>

const float TickRate = 60;
const int ServerPort = 10000;

#ifdef CLIENT

// ===================================================================================================================
//                                                       CLIENT
// ===================================================================================================================

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include "Fonts.h"
#include "GameClient.h"
#include "BSDSocket.h"
#include "NetworkSimulator.h"

using namespace protocol;

GameClient * client = nullptr;

Font * font = nullptr;

TimeBase timeBase;

static void clear_opengl_error()
{
    while ( glGetError() != GL_NO_ERROR );
}

static void check_opengl_error( const char * message )
{
    int error = glGetError();
    if ( error != GL_NO_ERROR )
    {
        printf( "%.2f: opengl error - %s (%s)\n", timeBase.time, gluErrorString( error ), message );
        exit( 1 );
    }    
}

static void init()
{
    /*
    GLuint vertexBuffer;
    glGenBuffers( 1, &vertexBuffer );
    printf( "%u\n", vertexBuffer );
    */

    /*
    
    glEnable( GL_DEPTH_TEST );
    glShadeModel( GL_SMOOTH );

    glFrontFace( GL_CW );
    glCullFace( GL_BACK );
    glEnable( GL_CULL_FACE );

    glEnable( GL_BLEND );
    glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
    */

    printf( "shader language version: %s\n", glGetString( GL_SHADING_LANGUAGE_VERSION ) );

    GLint compile_ok = GL_FALSE, link_ok = GL_FALSE;
 
    GLuint vs = glCreateShader( GL_VERTEX_SHADER );

    const char *vs_source = 
        "#version 410\n"  // OpenGL 4.1
        "attribute vec2 coord2d;                  "
        "void main(void) {                        "
        "  gl_Position = vec4(coord2d, 0.0, 1.0); "
        "}";

    glShaderSource( vs, 1, &vs_source, NULL);
    glCompileShader( vs );
    glGetShaderiv( vs, GL_COMPILE_STATUS, &compile_ok );
    if ( !compile_ok )
    {
        printf( "error: could not compile vertex shader\n" );
        char buffer[10*1024];
        GLsizei length = 0;
        glGetShaderInfoLog( vs, sizeof(buffer), &length, buffer );
        printf( "%s\n", buffer );
        exit(1);
    }

    glEnable( GL_FRAMEBUFFER_SRGB );

    glClearColor( 0.25, 0.25, 0.25, 0.0 );

    check_opengl_error( "before font load" );

    {
        // todo: create the font with allocator
        const char font_filename[] = "data/fonts/Console.font";
        font = new Font( font_filename );
        printf( "%.2f: Loaded font \"%s\"\n", timeBase.time, font_filename );
    }

    check_opengl_error( "after font load" );

    client = CreateGameClient( memory::default_allocator() );

    if ( !client )
    {
        printf( "%.2f: Failed to create game client!\n", timeBase.time );
        exit( 1 );
    }

    printf( "%.2f: Started game client on port %d\n", timeBase.time, client->GetPort() );

    Address address( "::1" );
    address.SetPort( ServerPort );

    client->Connect( address );

    timeBase.deltaTime = 1.0 / TickRate;
}

static void update()
{
    client->Update( timeBase );

    timeBase.time += timeBase.deltaTime;
}

static void render()
{
    check_opengl_error( "before render" );

    client->Update( timeBase );

    glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );

    GLfloat triangle_vertices[] = 
    {
         0.0,  0.8,
        -0.8, -0.8,
         0.8, -0.8
    };
        

    /*
    glLoadIdentity();

//    glColor3f( 0, 0, 0 );

    font->DrawString( 10, 200, "Hello my baby. Hello my darling. Hello my ragtime doll" );
    */

    check_opengl_error( "after render" );
}

static void shutdown()
{
    DestroyGameClient( memory::default_allocator(), client );

    delete font;
}

int main( int argc, char * argv[] )
{
    srand( time( nullptr ) );

    memory::initialize();

    if ( !InitializeNetwork() )
    {
        printf( "%.2f: Failed to initialize network!\n", timeBase.time );
        return 1;
    }

    PROTOCOL_ASSERT( IsNetworkInitialized() );

    glfwInit();

    glfwWindowHint( GLFW_CONTEXT_VERSION_MAJOR, 4 );
    glfwWindowHint( GLFW_CONTEXT_VERSION_MINOR, 1 );
    glfwWindowHint( GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE );
    glfwWindowHint( GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE );
    glfwWindowHint( GLFW_SRGB_CAPABLE, GL_TRUE );
    glfwWindowHint( GLFW_RESIZABLE, GL_TRUE );

    GLFWwindow * window = glfwCreateWindow( 1200, 800, "OpenGL", nullptr, nullptr );
    
    //GLFWwindow* window = glfwCreateWindow(800, 600, "OpenGL", glfwGetPrimaryMonitor(), nullptr); // Fullscreen    

    glfwMakeContextCurrent( window );

    glewExperimental = GL_TRUE;
    glewInit();

    clear_opengl_error();

    if ( !GLEW_VERSION_4_1 )
    {
        printf( "error: OpenGL 4.1 is not supported\n" );
        exit(1);
    }

    init();

    while ( !glfwWindowShouldClose( window ) )
    {
        glfwPollEvents();

        if ( glfwGetKey( window, GLFW_KEY_ESCAPE ) == GLFW_PRESS )
            glfwSetWindowShouldClose( window, GL_TRUE );

        update();

        render();

        glfwSwapBuffers( window );
    }

    shutdown();

    ShutdownNetwork();

    // IMPORTANT: Disabled until we fix leak issue with game client/server objects in config¡
    //memory::shutdown();

    glfwTerminate();
    
    return 0;
}

// ===================================================================================================================

#else

// ===================================================================================================================
//                                                       SERVER
// ===================================================================================================================

#include "GameServer.h"

const int MaxClients = 16;

int main( int argc, char ** argv )
{
    srand( time( nullptr ) );

    TimeBase timeBase;

    timeBase.deltaTime = 1.0 / TickRate;

    memory::initialize();
    
    if ( !InitializeNetwork() )
    {
        printf( "%.2f: Failed to initialize network!\n", timeBase.time );
        return 1;
    }

    auto server = CreateGameServer( memory::default_allocator(), ServerPort, MaxClients );

    if ( !server )
    {
        printf( "%.2f: Failed to not create server on port %d\n", timeBase.time, ServerPort );
        return 1;
    }
    
    printf( "%.2f: Started game server on port %d\n", timeBase.time, ServerPort );

    while ( true )
    {
        // ...

        // todo: rather than sleeping for MS we want a signal that comes in every n millis instead
        // so we maintain a steady tick rate. how best to do this on linux and mac? (need both...)

        // todo: want to detect the CTRL^C signal and actually break outta here

        server->Update( timeBase );

        sleep_milliseconds( timeBase.deltaTime * 1000 );

        timeBase.time += timeBase.deltaTime;
    }

    printf( "%.2f: Shutting down game server\n", timeBase.time );

    DestroyGameServer( memory::default_allocator(), server );

    ShutdownNetwork();

    memory::shutdown();

    return 0;
}

// ===================================================================================================================

#endif
