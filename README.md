# **ProtoPirate**

Uncommented line 29 in protopirate_app_i.h - #define ENABLE_EMULATE_FEATURE is now active

Updated application.fam - Added "subghz" to the requirements array to enable TX functionality

below TX:

requires=["gui","subghz"],

stack_size=2 * 1024, >>> changed to 8

### _for Flipper Zero_

ProtoPirate is an experimental rolling-code analysis toolkit developed by members of **The Pirates' Plunder**.
The app currently supports decoding for multiple automotive key-fob families (Kia, Ford, Subaru, Suzuki, VW, and more), with the goal of being a drop-in Flipper app (.fap) that is free, open source, and can be used on any Flipper Zero firmware.

## **Supported Protocols**

| Protocol                      | Decoder | Encoder |
|:------------------------------|:--------|:--------|
| Fiat V0                       | ✅ | ✅ |
| Ford V0                       | ✅ | ✅ |
| Kia V0 / V1 / V2 / V3 / V4    | ✅ | ✅ |
| Kia V5 / V6                   | ✅ | ❌ |
| Scher-Khan                    | ✅ | ❌ |
| StarLine                      | ✅ | ✅ |
| Subaru                        | ✅ | ✅ |
| Suzuki                        | ✅ | ✅ |
| PSA                           | ✅ | ✅ |
| Volkswagen (VW)               | ✅ | ❌ |

_More Coming Soon_

## **Features**

### 📡 Protocol Receiver

Real-time signal capture and decoding with animated radar display. Supports frequency hopping.

### 📂 Sub Decode

Load and analyze existing `.sub` files from your SD card. Browse `/ext/subghz/` to decode previously captured signals.

### ⏱️ Timing Tuner

Tool for protocol developers to compare real fob signal timing against protocol definitions.

- **Protocol Definition**: Expected short/long pulse durations and tolerance
- **Received Signal**: Measured timing from real fob (avg, min, max, sample count)
- **Analysis**: Difference from expected, jitter measurements
- **Conclusion**: Whether timing matches or needs adjustment with specific recommendations

## **Credits**

The following contributors are recognized for helping us keep open sourced projects and the freeware community alive.

### **App Development**

- RocketGod
- MMX
- Leeroy
- Skorp - Thanks, I sneaked a lot from Weather App!
- Vadim's Radio Driver

### **Protocol Magic**

- L0rdDiakon
- YougZ
- RocketGod
- MMX
- DoobTheGoober
- Skorp
- Slackware
- Trikk
- Wootini
- Li0ard
- Leeroy

### **Reverse Engineering Support**

- DoobTheGoober
- MMX
- NeedNotApply
- RocketGod
- Slackware
- Trikk
- Li0ard

## **Community & Support**

Join **The Pirates' Plunder** on Discord for development updates, testing, protocol research, community support, and a bunch of badasses doing fun shit:

➡️ **[https://discord.gg/thepirates](https://discord.gg/thepirates)**

<img width="1500" height="1000" alt="rocketgod_logo_transparent" src="https://github.com/user-attachments/assets/ad15b106-152c-4a60-a9e2-4d40dfa8f3c6" />
