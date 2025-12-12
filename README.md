# FPSP_Camera_FPC
This repo contains FPC schematics of the newly developed FPSP camera project on an FPC board, aiming for super-high-speed parallel image processing.

## Repository Structure
| Directory | Use |
| --------- | --- |
| archive_old/ | Archived initial design (**not working**) |
| custom_fp/ | Footprint from external libraries |
| docs/ | Documentation gathered from external sources |

## TODO
- [x] RP2350 Power Supply Layout
- [ ] Add Proper Power Sequencing
- [ ] Source RP2354 A4
- [ ] Source B2B Connector
- [x] B2B Connector Pinout + Layout
- [x] HSTX Layout


## 12/12/2025 PCB ordering notes

- order FR-4 board with SMT assembly, run thru **functionality tests**
- order FPC without SMT assembly, test the **FPC characteristics and stiffener position**
- RP2345A and ICM45686 IMU ordered separately to test the functionality. Once it passes the prototype test, order at JLCPCB via **global sourcing** and prep for next assembly

### TODO
 - [ ] Place JLCPCB order
 - [ ] functionality test, FR4 prototype
 - [ ] functionality test, FPC stiffener position
------ 
 - [ ] sourcing RP2345A
 - [ ] sourcing ICM45686
 - [ ] sourcing board-to-board connectors from Molex 
