# Configuration

@admonition{ Host authority | @htmlonly
By providing the host authority (URI + port), you can easily open each configuration option in the config UI.
<br>
<script src="configuration.js"></script>
<strong>Host authority: </strong> <input type="text" id="host-authority" value="localhost:47990">
@endhtmlonly
}

Sunshine will work with the default settings for most users. In some cases you may want to configure Sunshine further.

The default location for the configuration file is listed below. You can use another location if you
choose, by passing in the full configuration file path as the first argument when you start Sunshine.

**Example**
```bash
sunshine ~/sunshine_config.conf
```

The default location of the `apps.json` is the same as the configuration file. You can use a custom
location by modifying the configuration file.

**Default Config Directory**

| OS      | Location                                        |
|---------|-------------------------------------------------|
| Docker  | @code{}/config@endcode                          |
| FreeBSD | @code{}~/.config/sunshine@endcode               |
| Linux   | @code{}~/.config/sunshine@endcode               |
| macOS   | @code{}~/.config/sunshine@endcode               |
| Windows | @code{}%ProgramFiles%\\Sunshine\\config@endcode |

Although it is recommended to use the configuration UI, it is possible manually configure Sunshine by
editing the `conf` file in a text editor. Use the examples as reference.

## General

### locale

<table>
    <tr>
        <td>Description</td>
        <td colspan="2">
            The locale used for Sunshine's user interface.
        </td>
    </tr>
    <tr>
        <td>Default</td>
        <td colspan="2">@code{}
            en
            @endcode</td>
    </tr>
    <tr>
        <td>Example</td>
        <td colspan="2">@code{}
            locale = en
            @endcode</td>
    </tr>
    <tr>
        <td rowspan="20">Choices</td>
        <td>bg</td>
        <td>Bulgarian</td>
    </tr>
    <tr>
        <td>cs</td>
        <td>Czech</td>
    </tr>
    <tr>
        <td>de</td>
        <td>German</td>
    </tr>
    <tr>
        <td>en</td>
        <td>English</td>
    </tr>
    <tr>
        <td>en_GB</td>
        <td>English (UK)</td>
    </tr>
    <tr>
        <td>en_US</td>
        <td>English (United States)</td>
    </tr>
    <tr>
        <td>es</td>
        <td>Spanish</td>
    </tr>
    <tr>
        <td>fr</td>
        <td>French</td>
    </tr>
    <tr>
        <td>it</td>
        <td>Italian</td>
    </tr>
    <tr>
        <td>ja</td>
        <td>Japanese</td>
    </tr>
    <tr>
        <td>ko</td>
        <td>Korean</td>
    </tr>
    <tr>
        <td>pl</td>
        <td>Polish</td>
    </tr>
    <tr>
        <td>pt</td>
        <td>Portuguese</td>
    </tr>
    <tr>
        <td>pt_BR</td>
        <td>Portuguese (Brazilian)</td>
    </tr>
    <tr>
        <td>ru</td>
        <td>Russian</td>
    </tr>
    <tr>
        <td>sv</td>
        <td>Swedish</td>
    </tr>
    <tr>
        <td>tr</td>
        <td>Turkish</td>
    </tr>
    <tr>
        <td>uk</td>
        <td>Ukranian</td>
    </tr>
    <tr>
        <td>zh</td>
        <td>Chinese (Simplified)</td>
    </tr>
    <tr>
        <td>zh_TW</td>
        <td>Chinese (Traditional)</td>
    </tr>
</table>

### sunshine_name

<table>
    <tr>
        <td>Description</td>
        <td colspan="2">
            The name displayed by Moonlight.
        </td>
    </tr>
    <tr>
        <td>Default</td>
        <td colspan="2">PC hostname</td>
    </tr>
    <tr>
        <td>Example</td>
        <td colspan="2">@code{}
            sunshine_name = Sunshine
            @endcode</td>
    </tr>
</table>

### streamhub_socket

Absolute filesystem path of the Linux StreamHub Provider control socket.
Default: empty (source unconfigured). Environment variables and ~ are not expanded.
Example: `streamhub_socket = /run/user/1000/streamhub/control.sock`.

Provider inputs are published as Moonlight applications after the input directory connects.
Each stream requires a successful Provider media negotiation.
See [StreamHub integration](streamhub/README.md).

### streamhub_codecs

Explicit codecs verified for the configured Provider: `h264`, `hevc`, or
`h264,hevc` (default). Only 8-bit SDR 4:2:0 is advertised. These startup
capabilities do not bypass per-session negotiation or probe an encoder session.
Restart Sunshine after changing this setting.

### min_log_level

<table>
    <tr>
        <td>Description</td>
        <td colspan="2">
            The minimum log level printed to standard out.
        </td>
    </tr>
    <tr>
        <td>Default</td>
        <td colspan="2">@code{}
            info
            @endcode</td>
    </tr>
    <tr>
        <td>Example</td>
        <td colspan="2">@code{}
            min_log_level = info
            @endcode</td>
    </tr>
    <tr>
        <td rowspan="7">Choices</td>
        <td>verbose</td>
        <td>All logging message.
            @attention{This may negatively affect streaming performance.}</td>
    </tr>
    <tr>
        <td>debug</td>
        <td>Debug log messages and higher.
            @attention{This may negatively affect streaming performance.}</td>
    </tr>
    <tr>
        <td>info</td>
        <td>Informational log messages and higher.</td>
    </tr>
    <tr>
        <td>warning</td>
        <td>Warning log messages and higher.</td>
    </tr>
    <tr>
        <td>error</td>
        <td>Error log messages and higher.</td>
    </tr>
    <tr>
        <td>fatal</td>
        <td>Only fatal log messages.</td>
    </tr>
    <tr>
        <td>none</td>
        <td>No log messages.</td>
    </tr>
</table>

### notify_pre_releases

<table>
    <tr>
        <td>Description</td>
        <td colspan="2">
            Whether to be notified of new pre-release versions of Sunshine.
        </td>
    </tr>
    <tr>
        <td>Default</td>
        <td colspan="2">@code{}
            disabled
            @endcode</td>
    </tr>
    <tr>
        <td>Example</td>
        <td colspan="2">@code{}
            notify_pre_releases = disabled
            @endcode</td>
    </tr>
</table>

## Network

### upnp

<table>
    <tr>
        <td>Description</td>
        <td colspan="2">
            Sunshine will attempt to open ports for streaming over the internet.
        </td>
    </tr>
    <tr>
        <td>Default</td>
        <td colspan="2">@code{}
            disabled
            @endcode</td>
    </tr>
    <tr>
        <td>Example</td>
        <td colspan="2">@code{}
            upnp = enabled
            @endcode</td>
    </tr>
</table>

### address_family

<table>
    <tr>
        <td>Description</td>
        <td colspan="2">
            Set the address family that Sunshine will use.
        </td>
    </tr>
    <tr>
        <td>Default</td>
        <td colspan="2">@code{}
            ipv4
            @endcode</td>
    </tr>
    <tr>
        <td>Example</td>
        <td colspan="2">@code{}
            address_family = both
            @endcode</td>
    </tr>
    <tr>
        <td rowspan="2">Choices</td>
        <td>ipv4</td>
        <td>IPv4 only</td>
    </tr>
    <tr>
        <td>both</td>
        <td>IPv4+IPv6</td>
    </tr>
</table>

### bind_address

<table>
    <tr>
        <td>Description</td>
        <td colspan="2">
            Set the IP address to bind Sunshine to. This is useful when you have multiple network interfaces
            and want to restrict Sunshine to a specific one. If not set, Sunshine will bind to all available
            interfaces (0.0.0.0 for IPv4 or :: for IPv6).
            <br><br>
            <strong>Note:</strong> The address must be valid for the system and must match the address family
            being used. When using IPv6, you can specify an IPv6 address even with address_family set to "both".
        </td>
    </tr>
    <tr>
        <td>Default</td>
        <td colspan="2">
            Empty, binds to all interfaces
            </td>
    </tr>
    <tr>
        <td>Example (IPv4)</td>
        <td colspan="2">@code{}
            bind_address = 192.168.1.100
            @endcode</td>
    </tr>
    <tr>
        <td>Example (IPv6)</td>
        <td colspan="2">@code{}
            bind_address = 2001:db8::1
            @endcode</td>
    </tr>
    <tr>
        <td>Example (Loopback)</td>
        <td colspan="2">@code{}
            bind_address = 127.0.0.1
            @endcode</td>
    </tr>
</table>

### port

<table>
    <tr>
        <td>Description</td>
        <td colspan="2">
            Set the family of ports used by Sunshine.
            Changing this value will offset other ports as shown in config UI.
        </td>
    </tr>
    <tr>
        <td>Default</td>
        <td colspan="2">@code{}
            47989
            @endcode</td>
    </tr>
    <tr>
        <td>Range</td>
        <td colspan="2">1029-65514</td>
    </tr>
    <tr>
        <td>Example</td>
        <td colspan="2">@code{}
            port = 47989
            @endcode</td>
    </tr>
</table>

### origin_web_ui_allowed

<table>
    <tr>
        <td>Description</td>
        <td colspan="2">
            The origin of the remote endpoint address that is not denied for HTTPS Web UI.
        </td>
    </tr>
    <tr>
        <td>Default</td>
        <td colspan="2">@code{}
            lan
            @endcode</td>
    </tr>
    <tr>
        <td>Example</td>
        <td colspan="2">@code{}
            origin_web_ui_allowed = lan
            @endcode</td>
    </tr>
    <tr>
        <td rowspan="3">Choices</td>
        <td>pc</td>
        <td>Only localhost may access the web ui</td>
    </tr>
    <tr>
        <td>lan</td>
        <td>Only LAN devices may access the web ui</td>
    </tr>
    <tr>
        <td>wan</td>
        <td>Anyone may access the web ui</td>
    </tr>
</table>

### csrf_allowed_origins

<table>
    <tr>
        <td>Description</td>
        <td colspan="2">
            Comma-separated list of additional allowed origins for CSRF protection. These origins will be
            appended to the default allowed origins (localhost variants and the configured web UI port).
            Requests from allowed origins can access state-changing API endpoints without CSRF tokens.
            <br><br>
            @attention{Only add origins you trust. Each origin must be a complete URL prefix
            including protocol and host (e.g., https://example.com). Port numbers are optional.}
        </td>
    </tr>
    <tr>
        <td>Default</td>
        <td colspan="2">
            Empty, uses built-in defaults: https://localhost, https://127.0.0.1, https://[::1],
            with configured UI port variants
            </td>
    </tr>
    <tr>
        <td>Example</td>
        <td colspan="2">@code{}
            csrf_allowed_origins = https://myapp.local,https://custom.domain.com
            @endcode</td>
    </tr>
</table>

### external_ip

<table>
    <tr>
        <td>Description</td>
        <td colspan="2">
            If no external IP address is given, Sunshine will attempt to automatically detect external ip-address.
        </td>
    </tr>
    <tr>
        <td>Default</td>
        <td colspan="2">Automatic</td>
    </tr>
    <tr>
        <td>Example</td>
        <td colspan="2">@code{}
            external_ip = 123.456.789.12
            @endcode</td>
    </tr>
</table>

### lan_encryption_mode

<table>
    <tr>
        <td>Description</td>
        <td colspan="2">
            This determines when encryption will be used when streaming over your local network.
            @warning{Encryption can reduce streaming performance, particularly on less powerful hosts and clients.}
        </td>
    </tr>
    <tr>
        <td>Default</td>
        <td colspan="2">@code{}
            0
            @endcode</td>
    </tr>
    <tr>
        <td>Example</td>
        <td colspan="2">@code{}
            lan_encryption_mode = 0
            @endcode</td>
    </tr>
    <tr>
        <td rowspan="3">Choices</td>
        <td>0</td>
        <td>encryption will not be used</td>
    </tr>
    <tr>
        <td>1</td>
        <td>encryption will be used if the client supports it</td>
    </tr>
    <tr>
        <td>2</td>
        <td>encryption is mandatory and unencrypted connections are rejected</td>
    </tr>
</table>

### wan_encryption_mode

<table>
    <tr>
        <td>Description</td>
        <td colspan="2">
            This determines when encryption will be used when streaming over the Internet.
            @warning{Encryption can reduce streaming performance, particularly on less powerful hosts and clients.}
        </td>
    </tr>
    <tr>
        <td>Default</td>
        <td colspan="2">@code{}
            1
            @endcode</td>
    </tr>
    <tr>
        <td>Example</td>
        <td colspan="2">@code{}
            wan_encryption_mode = 1
            @endcode</td>
    </tr>
    <tr>
        <td rowspan="3">Choices</td>
        <td>0</td>
        <td>encryption will not be used</td>
    </tr>
    <tr>
        <td>1</td>
        <td>encryption will be used if the client supports it</td>
    </tr>
    <tr>
        <td>2</td>
        <td>encryption is mandatory and unencrypted connections are rejected</td>
    </tr>
</table>

### ping_timeout

<table>
    <tr>
        <td>Description</td>
        <td colspan="2">
            How long to wait, in milliseconds, for data from Moonlight before shutting down the stream.
        </td>
    </tr>
    <tr>
        <td>Default</td>
        <td colspan="2">@code{}
            10000
            @endcode</td>
    </tr>
    <tr>
        <td>Example</td>
        <td colspan="2">@code{}
            ping_timeout = 10000
            @endcode</td>
    </tr>
</table>

### packetsize

<table>
    <tr>
        <td>Description</td>
        <td colspan="2">
            Limit the packetsize to avoid fragmentation on a low MTU link.
            @note{This helps avoid packet loss and micro-stutter on a layer 2 VPN with
            clients that cannot configure this value, e.g. Moonlight for Android/iOS.
            }
            @tip{To discover the optimal value:
            <ul>
                <li>Send ping to the server with don't fragment flag (DF)</li>
                <li>Find the size of the largest replay, and subtract 16</li>
                <li>Monitor the traffic to ensure no fragmentation</li>
            </ul>
            If using a VPN tunnel:
            <ul>
                <li>Set MTU on the TUN/TAP interface, and</li>
                <li>Ensure no fragmentation both inside and outside the tunnel</li>
                <li>Max UDP size = MTU size - 28</li>
                <li>`packetsize` = max UDP size - 16</li>
                <li>Monitor the traffic to ensure no fragmentation</li>
            </ul>
            Sample calculation for OpenVPN layer 2, using IPv4:
            <ul>
                <li>1428 bytes for max ICMP/UDP size outside the tunnel</li>
                <li>Subtract the OpenVPN overhead: 24 bytes (may vary)</li>
                <li>1404 bytes for Ethernet inside the tunnel</li>
                <li>Subtract the Ethernet header: 14 bytes</li>
                <li>1390 bytes for MTU inside the tunnel</li>
                <li>Subtract the IPv4 header: 20 bytes</li>
                <li>Subtract the UDP header: 8 bytes</li>
                <li>1362 bytes for UDP payload</li>
                <li>Subtract: 16 bytes</li>
                <li>1346 bytes for `packetsize`</li>
            </ul>
            }
            @warning{Reduce the bitrate when using low values.
            Values larger than 1456 require jumbo frames.
            }
        </td>
    </tr>
    <tr>
        <td>Default</td>
        <td colspan="2">@code{}
            0
            @endcode</td>
    </tr>
    <tr>
        <td>Range</td>
        <td colspan="2">0, 200-65535</td>
    </tr>
    <tr>
        <td>Example</td>
        <td colspan="2">@code{}
            packetsize = 1346
            @endcode</td>
    </tr>
</table>

## Config Files

### file_apps

<table>
    <tr>
        <td>Description</td>
        <td colspan="2">
            The application configuration file path. The file contains a JSON formatted list of applications that
            can be started by Moonlight.
        </td>
    </tr>
    <tr>
        <td>Default</td>
        <td colspan="2">@code{}
            apps.json
            @endcode</td>
    </tr>
    <tr>
        <td>Example</td>
        <td colspan="2">@code{}
            file_apps = apps.json
            @endcode</td>
    </tr>
</table>

### credentials_file

<table>
    <tr>
        <td>Description</td>
        <td colspan="2">
            The file where user credentials for the UI are stored.
        </td>
    </tr>
    <tr>
        <td>Default</td>
        <td colspan="2">@code{}
            sunshine_state.json
            @endcode</td>
    </tr>
    <tr>
        <td>Example</td>
        <td colspan="2">@code{}
            credentials_file = sunshine_state.json
            @endcode</td>
    </tr>
</table>

### log_path

<table>
    <tr>
        <td>Description</td>
        <td colspan="2">
            The path where the current Sunshine log is stored. Each time Sunshine starts, up to five previous
            logs are retained by appending <code>.1</code> through <code>.5</code> to this path.
        </td>
    </tr>
    <tr>
        <td>Default</td>
        <td colspan="2">@code{}
            sunshine.log
            @endcode</td>
    </tr>
    <tr>
        <td>Example</td>
        <td colspan="2">@code{}
            log_path = sunshine.log
            @endcode</td>
    </tr>
</table>

### pkey

<table>
    <tr>
        <td>Description</td>
        <td colspan="2">
            The private key used for the web UI and Moonlight client pairing. For best compatibility,
            this should be an RSA-2048 private key.
            @warning{Not all Moonlight clients support ECDSA keys or RSA key lengths other than 2048 bits.}
        </td>
    </tr>
    <tr>
        <td>Default</td>
        <td colspan="2">@code{}
            credentials/cakey.pem
            @endcode</td>
    </tr>
    <tr>
        <td>Example</td>
        <td colspan="2">@code{}
            pkey = /dir/pkey.pem
            @endcode</td>
    </tr>
</table>

### cert

<table>
    <tr>
        <td>Description</td>
        <td colspan="2">
            The certificate used for the web UI and Moonlight client pairing. For best compatibility,
            this should have an RSA-2048 public key.
            @warning{Not all Moonlight clients support ECDSA keys or RSA key lengths other than 2048 bits.}
        </td>
    </tr>
    <tr>
        <td>Default</td>
        <td colspan="2">@code{}
            credentials/cacert.pem
            @endcode</td>
    </tr>
    <tr>
        <td>Example</td>
        <td colspan="2">@code{}
            cert = /dir/cert.pem
            @endcode</td>
    </tr>
</table>

### file_state

<table>
    <tr>
        <td>Description</td>
        <td colspan="2">
            The file where current state of Sunshine is stored.
        </td>
    </tr>
    <tr>
        <td>Default</td>
        <td colspan="2">@code{}
            sunshine_state.json
            @endcode</td>
    </tr>
    <tr>
        <td>Example</td>
        <td colspan="2">@code{}
            file_state = sunshine_state.json
            @endcode</td>
    </tr>
</table>

## Advanced

### fec_percentage

<table>
    <tr>
        <td>Description</td>
        <td colspan="2">
            Percentage of error correcting packets per data packet in each video frame.
            @warning{Higher values can correct for more network packet loss,
            but at the cost of increasing bandwidth usage.}
        </td>
    </tr>
    <tr>
        <td>Default</td>
        <td colspan="2">@code{}
            20
            @endcode</td>
    </tr>
    <tr>
        <td>Range</td>
        <td colspan="2">1-255</td>
    </tr>
    <tr>
        <td>Example</td>
        <td colspan="2">@code{}
            fec_percentage = 20
            @endcode</td>
    </tr>
</table>

### max_bitrate

<table>
    <tr>
        <td>Description</td>
        <td colspan="2">
            The maximum bitrate (in Kbps) that Sunshine will encode the stream at. If set to 0, it will always use the bitrate requested by Moonlight.
        </td>
    </tr>
    <tr>
        <td>Default</td>
        <td colspan="2">@code{}
            0
            @endcode</td>
    </tr>
    <tr>
        <td>Example</td>
        <td colspan="2">@code{}
            max_bitrate = 5000
            @endcode</td>
    </tr>
</table>


<div class="section_buttons">

| Previous          |                            Next |
|:------------------|--------------------------------:|
| [Legal](legal.md) | [App Examples](app_examples.md) |

</div>

<details style="display: none;">
  <summary></summary>
  [TOC]
</details>
