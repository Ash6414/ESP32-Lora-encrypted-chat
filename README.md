# Gossiper
### Privacy, consent, and community care in a local ESP32 + LoRa messaging system

This project is a small encrypted messaging system built with **two ESP32-WROOM-D boards** and **two RYLR 915 MHz LoRa modules**. Each ESP32 creates a private Wi-Fi access point and hosts a local web page that allows a user to send and receive messages. The devices communicate with each other over **LoRa**, while the Wi-Fi side is used only for local access from a phone or laptop.

This project was designed with a women and gender studies framework in mind. That means the technology is not only functional, but also intentional: privacy, consent, access, and care are built into the design. The public-facing system stays neutral, while the interface includes opt-in feminist easter eggs that highlight values like solidarity, safety, and community.

---

## Project goals

This system was built to explore how technical design can reflect social values.

It aims to:

- support **local, device-to-device communication**
- reduce dependence on cloud platforms
- protect message content through **end-to-end encryption**
- encourage thoughtful design around **privacy and consent**
- demonstrate how activism, ethics, and engineering can work together

---

## Features

### Core system
- ESP32-hosted local web interface
- LoRa messaging between two nodes
- 915 MHz operation
- UART communication between ESP32 and RYLR module
- separate node addresses for point-to-point messaging

### Security
- **AES-256-GCM encryption** for LoRa messages
- message authentication
- replay protection
- Wi-Fi access point password
- HTTP Auth for web login
- logs stored in **RAM only** and cleared on reboot
- neutral SSIDs to avoid publicly revealing the project topic

### Interface and theme
- consent screen before entering the chat
- rotating micro-manifesto lines
- hidden **Solidarity Mode**
- hidden `/zine` page with “Field Notes on Care”
- feminist easter eggs built into the site in a non-intrusive way

---

## Hardware used

- 2 × ESP32-WROOM-D boards
- 2 × RYLR LoRa modules
- USB cables / power source
- phone or laptop for connecting to the ESP32 web page

---

## Wiring

For each ESP32 + LoRa pair:

- **LoRa RXD → ESP32 GPIO17**
- **LoRa TXD → ESP32 GPIO16**
- **LoRa VDD → ESP32 3.3V**
- **LoRa GND → ESP32 GND**

### UART mapping
- ESP32 GPIO17 = TX
- ESP32 GPIO16 = RX

---

## Frequency

This build is configured for:

- **915 MHz**

The sketch uses:

```cpp
const uint32_t LORA_BAND = 915000000;
