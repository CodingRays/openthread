# OpenThread CLI - Network Data

## Overview

Thread Network Data contains information about Border Routers and other servers available in the Thread network. Border Routers and devices offering services register their information with the Leader. The Leader collects and structures this information within the Thread Network Data and distributes the information to all devices in the Thread Network.

Border Routers may register prefixes assigned to the Thread Network and prefixes that they offer routes for. Services may register any information relevant to the service itself.

Border Router and service information may be stable or temporary. Stable Thread Network Data is distributed to all devices, including Sleepy End Devices (SEDs). Temporary Network Data is distributed to all nodes except SEDs.

## Quick Start

### Form Network and Configure Prefix

1. Generate and view new network configuration.

   ```bash
   > dataset init new
   Done
   > dataset
   Active Timestamp: 1
   Channel: 13
   Channel Mask: 0x07fff800
   Ext PAN ID: d63e8e3e495ebbc3
   Mesh Local Prefix: fd3d:b50b:f96d:722d::/64
   Network Key: dfd34f0f05cad978ec4e32b0413038ff
   Network Name: OpenThread-8f28
   PAN ID: 0x8f28
   PSKc: c23a76e98f1a6483639b1ac1271e2e27
   Security Policy: 0, onrc
   Done
   ```

2. Commit new dataset to the Active Operational Dataset in non-volatile storage.

   ```bash
   > dataset commit active
   Done
   ```

3. Enable Thread interface

   ```bash
   > ifconfig up
   Done
   > thread start
   Done
   ```

4. Observe IPv6 addresses assigned to the Thread interface.

   ```bash
   > ipaddr
   fd3d:b50b:f96d:722d:0:ff:fe00:fc00
   fd3d:b50b:f96d:722d:0:ff:fe00:dc00
   fd3d:b50b:f96d:722d:393c:462d:e8d2:db32
   fe80:0:0:0:a40b:197f:593d:ca61
   Done
   ```

5. Register an IPv6 prefix assigned to the Thread network.

   ```bash
   > prefix add fd00:dead:beef:cafe::/64 paros
   Done
   > netdata register
   Done
   ```

6. Observe Thread Network Data.

   ```bash
   > netdata show
   Prefixes:
   fd00:dead:beef:cafe::/64 paros med dc00
   Routes:
   Services:
   Done
   ```

7. Observe IPv6 addresses assigned to the Thread interface.

   ```bash
   > ipaddr
   fd00:dead:beef:cafe:4da8:5234:4aa2:4cfa
   fd3d:b50b:f96d:722d:0:ff:fe00:fc00
   fd3d:b50b:f96d:722d:0:ff:fe00:dc00
   fd3d:b50b:f96d:722d:393c:462d:e8d2:db32
   fe80:0:0:0:a40b:197f:593d:ca61
   Done
   ```

### Attach to Existing Network

Only the Network Key is required for a device to attach to a Thread network.

While not required, specifying the channel avoids the need to search across multiple channels, improving both latency and efficiency of the attach process.

After the device successfully attaches to a Thread network, the device will retrieve the complete Active Operational Dataset.

1. Create a partial Active Operational Dataset.

   ```bash
   > dataset networkkey dfd34f0f05cad978ec4e32b0413038ff
   Done
   > dataset commit active
   Done
   ```

2. Enable Thread interface.

   ```bash
   > ifconfig up
   Done
   > thread start
   Done
   ```

3. After attaching, observe Thread Network Data.

   ```bash
   > netdata show
   Prefixes:
   fd00:dead:beef:cafe::/64 paros med dc00
   Routes:
   Services:
   Done
   ```

4. Observe IPv6 addresses assigned to the Thread interface.

   ```bash
   > ipaddr
   fd00:dead:beef:cafe:4da8:5234:4aa2:4cfa
   fd3d:b50b:f96d:722d:0:ff:fe00:fc00
   fd3d:b50b:f96d:722d:0:ff:fe00:dc00
   fd3d:b50b:f96d:722d:393c:462d:e8d2:db32
   fe80:0:0:0:a40b:197f:593d:ca61
   Done
   ```

## Command List

- [help](#help)
- [full](#full)
- [length](#length)
- [maxlength](#maxlength)
- [publish](#publish)
- [register](#register)
- [show](#show)
- [steeringdata](#steeringdata-check-eui64discerner)
- [unpublish](#unpublish)

## Command Details

### help

Usage: `netdata help`

Print netdata help menu.

```bash
> netdata help
full
length
maxlength
publish
register
show
steeringdata
unpublish
Done
```

### full

Usage: `netdata full`

Print "yes" or "no" flag tracking whether or not the "net data full" callback has been invoked since start of Thread operation or since the last time `netdata full reset` was used to reset the flag.

This command requires `OPENTHREAD_CONFIG_BORDER_ROUTER_SIGNAL_NETWORK_DATA_FULL`.

The "net data full" callback is invoked whenever:

- The device is acting as a leader and receives a Network Data registration from a Border Router (BR) that it cannot add to Network Data (running out of space).
- The device is acting as a BR and new entries cannot be added to its local Network Data.
- The device is acting as a BR and tries to register its local Network Data entries with the leader, but determines that its local entries will not fit.

```bash
> netdata full
no
Done
```

### full reset

Usage: `netdata full reset`

Reset the flag tracking whether "net data full" callback was invoked.

This command requires `OPENTHREAD_CONFIG_BORDER_ROUTER_SIGNAL_NETWORK_DATA_FULL`.

```bash
> netdata full reset
Done
```

### length

Usage: `netdata length`

Get the current length of (number of bytes) Partition's Thread Network Data.

```bash
> netdata length
23
Done
```

### maxlength

Usage: `netdata maxlength`

Get the maximum observed length of the Thread Network Data since OT stack initialization or since the last call to `netdata maxlength reset`.

```bash
> netdata maxlength
40
Done
```

### maxlength reset

Usage: `netdata maxlength reset`

Reset the tracked maximum length of the Thread Network Data.

```bash
> netdata maxlength reset
Done
```

### publish

The Network Data Publisher provides mechanisms to limit the number of similar Service and/or Prefix (on-mesh prefix or external route) entries in the Thread Network Data by monitoring the Network Data and managing if or when to add or remove entries.

The Publisher requires `OPENTHREAD_CONFIG_NETDATA_PUBLISHER_ENABLE`.

### publish dnssrp

Publish DNS/SRP service entry.

This command requires `OPENTHREAD_CONFIG_TMF_NETDATA_SERVICE_ENABLE`.

The following formats are available:

- `netdata publish dnssrp anycast <seq-num> [<version>]` to publish "DNS/SRP Service Anycast Address" with a given sequence number and version.
- `netdata publish dnssrp unicast <address> <port> [<version>]` to publish "DNS/SRP Service Unicast Address" with given address, port number and version info. The address/port/version info is included in Service TLV data.
- `netdata publish dnssrp unicast <port> [<version>]` to publish "DNS/SRP Service Unicast Address" with given port number, version, and the device's mesh-local EID for the address. The address/port/version info is included in Server TLV data.

A new call to `netdata publish dnssrp [anycast|unicast] [...]` command will remove and replace any previous "DNS/SRP Service" entry that was being published (from earlier `netdata publish dnssrp [...]` commands).

```bash
> netdata publish dnssrp anycast 1 2
Done

> netdata publish dnssrp unicast fd00::1234 51525 1
Done

> netdata publish dnssrp unicast 50152 2
Done
```

### publish prefix \<prefix\> [padcrosnD][prf]

Publish an on-mesh prefix entry.

- p: Preferred flag
- a: Stateless IPv6 Address Autoconfiguration flag
- d: DHCPv6 IPv6 Address Configuration flag
- c: DHCPv6 Other Configuration flag
- r: Default Route flag
- o: On Mesh flag
- s: Stable flag
- n: Nd Dns flag
- D: Domain Prefix flag (only available for Thread 1.2).
- prf: Preference, which may be 'high', 'med', or 'low'.

```bash
> netdata publish prefix fd00:1234:5678::/64 paos med
Done
```

### publish route \<prefix\> [sn][prf]

Publish an external route entry.

- s: Stable flag
- n: NAT64 flag
- a: Advertising PIO (AP) flag
- prf: Preference, which may be: 'high', 'med', or 'low'.

```bash
> netdata publish route fd00:1234:5678::/64 s high
Done
```

### publish replace \<old prefix\> \<prefix\> [sn][prf]

Replace a previously published external route entry.

If there is no previously published external route matching old prefix, this command behaves similarly to `netdata publish route`. If there is a previously published route entry, it will be replaced with the new prefix. In particular, if the old prefix was already added in the Network Data, the change to the new prefix is immediately reflected in the Network Data (i.e., old prefix is removed and the new prefix is added in the same Network Data registration request to leader). This ensures that route entries in the Network Data are not abruptly removed.

- s: Stable flag
- n: NAT64 flag
- a: Advertising PIO (AP) flag
- prf: Preference, which may be: 'high', 'med', or 'low'.

```bash
> netdata publish replace ::/0 fd00:1234:5678::/64 s high
Done
```

### register

Usage: `netdata register`

Register configured prefixes, routes, and services with the Leader.

```bash
> netdata register
Done
```

### show

Usage: `netdata show [local] [-x] [\<rloc16\>]`

Print entries in Network Data, on-mesh prefixes, external routes, services, and 6LoWPAN context information.

If the optional `rloc16` input is specified, prints the entries associated with the given RLOC16 only. The RLOC16 filtering can be used when `-x` or `local` are not used.

On-mesh prefixes are listed under `Prefixes` header:

- The on-mesh prefix
- Flags
  - p: Preferred flag
  - a: Stateless IPv6 Address Autoconfiguration flag
  - d: DHCPv6 IPv6 Address Configuration flag
  - c: DHCPv6 Other Configuration flag
  - r: Default Route flag
  - o: On Mesh flag
  - s: Stable flag
  - n: Nd Dns flag
  - D: Domain Prefix flag (only available for Thread 1.2).
- Preference `high`, `med`, or `low`
- RLOC16 of device which added the on-mesh prefix

External Routes are listed under `Routes` header:

- The route prefix
- Flags
  - s: Stable flag
  - n: NAT64 flag
  - a: Advertising PIO (AP) flag
- Preference `high`, `med`, or `low`
- RLOC16 of device which added the route prefix

Service entries are listed under `Services` header:

- Enterprise number
- Service data (as hex bytes)
- Server data (as hex bytes)
- Flags
  - s: Stable flag
- RLOC16 of devices which added the service entry
- Service ID

6LoWPAN Context IDs are listed under `Contexts` header:

- The prefix
- Context ID
- Flags:
  - s: Stable flag
  - c: Compress flag

When there are no other flags, `-` will be used.

Commissioning Dataset information is printed under `Commissioning` header:

- Session ID if present in Dataset or `-` otherwise
- Border Agent RLOC16 (in hex) if present in Dataset or `-` otherwise
- Joiner UDP port number if present in Dataset or `-` otherwise
- Steering Data (as hex bytes) if present in Dataset or `-` otherwise
- Flags:
  - e: if Dataset contains any extra unknown TLV

Print Network Data received from the Leader.

```bash
> netdata show
Prefixes:
fd00:dead:beef:cafe::/64 paros med a000
Routes:
fd00:1234:0:0::/64 s med a000
fd00:4567:0:0::/64 s med 8000
Services:
44970 5d fddead00beef00007bad0069ce45948504d2 s a000 0
Contexts:
fd00:dead:beef:cafe::/64 1 sc
Commissioning:
1248 dc00 9988 00000000000120000000000000000000 e
Done
```

Print Network Data entries from the Leader associated with `0xa00` RLOC16.

```bash
> netdata show 0xa00
Prefixes:
fd00:dead:beef:cafe::/64 paros med a000
Routes:
fd00:1234:0:0::/64 s med a000
Services:
44970 5d fddead00beef00007bad0069ce45948504d2 s a000 0
Done
```

Print Network Data received from the Leader as hex-encoded TLVs.

```bash
> netdata show -x
08040b02174703140040fd00deadbeefcafe0504dc00330007021140
Done
```

Print local Network Data to sync with Leader.

```bash
> netdata show local
Prefixes:
fd00:dead:beef:cafe::/64 paros med dc00
Routes:
Services:
Done
```

Print local Network Data to sync with Leader as hex-encoded TLVs.

```bash
> netdata show local -x
08040b02174703140040fd00deadbeefcafe0504dc00330007021140
Done
```

### netdata steeringdata check \<eui64\>|\<discerner\>

Check whether the steering data includes a joiner.

- eui64: The IEEE EUI-64 of the Joiner.
- discerner: The Joiner discerner in format `number/length`.

```bash
> netdata steeringdata check d45e64fa83f81cf7
Done
> netdata steeringdata check 0xabc/12
Done
> netdata steeringdata check 0xdef/12
Error 23: NotFound
```

### unpublish

This command unpublishes a previously published Network Data entry.

This command requires `OPENTHREAD_CONFIG_NETDATA_PUBLISHER_ENABLE`.

### unpublish dnssrp

Unpublishes DNS/SRP Service entry (available when `OPENTHREAD_CONFIG_TMF_NETDATA_SERVICE_ENABLE` is enabled):

- `netdata unpublish dnssrp` to unpublish "DNS/SRP Service" entry (anycast or unciast).

```bash
> netdata unpublish dnssrp
Done
```

### unpublish \<prefix\>

Unpublishes a previously published on-mesh prefix or external route entry.

```bash
> netdata unpublish fd00:1234:5678::/64
Done
```
