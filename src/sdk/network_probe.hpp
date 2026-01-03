#pragma once

// NetworkManager Discovery Probe
// Diagnóstico para encontrar clases de red ofuscadas en Lunar Client
//
// Uso: Llamar NetworkProbe::run() una vez después de SDK_Init()
// Los resultados se imprimen en el debug console.

namespace NetworkProbe {
    // Run all discovery methods and log results
    void run();
    
    // Individual probes
    void probeMCPNames();           // Method 1: Try standard MCP names
    void probePlayerFields();       // Method 2: Enumerate EntityPlayerSP fields
    void probeNetworkClasses();     // Method 3: Search all loaded classes
    void probeNettyHandlers();      // Method 4: Find Netty ChannelHandlers
}
