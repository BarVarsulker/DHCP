# DHCP Server and Client Implementation

## Overview
This repository contains an educational implementation of a basic **DHCP server and client**, written in **C**.

The **DHCP server** dynamically assigns IP addresses from a predefined pool to clients on the network. The **DHCP client** requests network configuration using the DHCP protocol and receives an IP address, lease time, and other settings.

> 📄 Full explanations and design details are provided in the attached presentation (`presentation.pdf`).

## Features
- DHCPDISCOVER, DHCPOFFER, DHCPREQUEST, and DHCPACK message flow
- IP address pool management
- Lease time handling
- Basic CLI menus for client and server
- Logging of server-client communication

## Usage
Compile using `gcc` (or any C compiler):

```bash
gcc DHCP_server.c -o server
gcc DHCP_client.c -o client
