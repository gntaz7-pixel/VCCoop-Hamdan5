# Mimics the GUEST end of the current v0.6 wire protocol without starting a second Bully.
# Sends to loopback ONLY, never touches Bully memory or saves.
$ErrorActionPreference = 'Stop'
$hostIP = [System.Net.IPAddress]::Parse('127.0.0.1')
$hostPort = 7791
$code = [uint32]246813
$magic = [byte[]] @(0x42,0x43,0x4F,0x50) # BCOP in network order

function Put-BE16([byte[]] $buffer, [int] $offset, [uint16] $v) {
    $buffer[$offset] = [byte](($v -shr 8) -band 255)
    $buffer[$offset+1] = [byte]($v -band 255)
}
function Put-BE32([byte[]] $buffer, [int] $offset, [uint32] $v) {
    $buffer[$offset] = [byte](($v -shr 24) -band 255)
    $buffer[$offset+1] = [byte](($v -shr 16) -band 255)
    $buffer[$offset+2] = [byte](($v -shr 8) -band 255)
    $buffer[$offset+3] = [byte]($v -band 255)
}
function Read-BE16([byte[]] $b,[int] $off) {
    return ([int]$b[$off] -shl 8) -bor [int]$b[$off+1]
}
function Read-BE32([byte[]] $b,[int] $off) {
    return ([uint32]$b[$off] -shl 24) -bor ([uint32]$b[$off+1] -shl 16) -bor ([uint32]$b[$off+2] -shl 8) -bor [uint32]$b[$off+3]
}
function Build-GuestPacket([uint32] $seq,[single] $x,[single] $y,[single] $z) {
    $p = New-Object byte[] 28
    [System.Array]::Copy($magic, 0, $p, 0, 4)
    Put-BE16 $p 4 ([uint16]1)
    Put-BE16 $p 6 ([uint16]2) # guest
    Put-BE32 $p 8 $code
    Put-BE32 $p 12 $seq
    [System.Array]::Copy([BitConverter]::GetBytes($x), 0, $p, 16, 4)
    [System.Array]::Copy([BitConverter]::GetBytes($y), 0, $p, 20, 4)
    [System.Array]::Copy([BitConverter]::GetBytes($z), 0, $p, 24, 4)
    return ,$p
}
function Is-ValidHostPacket([byte[]] $p) {
    if ($p.Length -ne 28) { return $false }
    if ($p[0] -ne 0x42 -or $p[1] -ne 0x43 -or $p[2] -ne 0x4F -or $p[3] -ne 0x50) { return $false }
    if ((Read-BE16 $p 4) -ne 1 -or (Read-BE16 $p 6) -ne 1) { return $false }
    if ((Read-BE32 $p 8) -ne $code) { return $false }
    return $true
}

$udp = $null
try {
    $udp = New-Object System.Net.Sockets.UdpClient(0)
    $udp.Connect($hostIP, $hostPort)
    $udp.Client.ReceiveTimeout = 150
    $sender = New-Object System.Net.IPEndPoint([System.Net.IPAddress]::Any, 0)
    Write-Host 'FAKE GUEST v0.14 -> SAFE 4.0m orbit; FLAT GROUND ONLY; run ONE fake client' -ForegroundColor Cyan
    Write-Host 'Start Bully.exe as HOST FIRST. Close OTHER FAKE GUEST windows (host accepts one sender).'
    Write-Host 'Waiting for HOST replies; press Ctrl+C to stop.'
    $sequence = [uint32]0
    $t = [System.Diagnostics.Stopwatch]::StartNew()
    $sent = 0
    $received = 0
    $lastPrint = [int64]-3000
    $lastWarn = [int64]0
    $baseX = [single]298.0
    $baseY = [single]-72.0
    $baseZ = [single]5.80
    $knownHost = $false
    while ($true) {
        $sequence = [uint32]($sequence + 1)
        $seconds = $t.Elapsed.TotalSeconds
        # Flat-area test only: 4.0m radius (outside Jimmy collision zone), slow circle.
        # No game injection here: pure UDP test coordinates.
        $x = [single]($baseX + 4.0 * [Math]::Cos($seconds * 0.15))
        $y = [single]($baseY + 4.0 * [Math]::Sin($seconds * 0.15))
        $z = [single]$baseZ
        $packet = Build-GuestPacket $sequence $x $y $z
        [void]$udp.Send($packet, $packet.Length)
        $sent++
        for ($i=0; $i -lt 8 -and $udp.Available -gt 0; $i++) {
            try {
                $response = $udp.Receive([ref] $sender)
                if ((Is-ValidHostPacket $response) -and $sender.Address.Equals($hostIP) -and $sender.Port -eq $hostPort) {
                    $received++
                    $baseX = [BitConverter]::ToSingle($response,16)
                    $baseY = [BitConverter]::ToSingle($response,20)
                    $baseZ = [BitConverter]::ToSingle($response,24)
                    if (-not $knownHost) {
                        Write-Host 'HOST FOUND! Safe orbit active. Stay in an OPEN FLAT space.' -ForegroundColor Green
                        $knownHost = $true
                    }
                    if ($t.ElapsedMilliseconds - $lastPrint -ge 2000) {
                        $rx = $baseX
                        $ry = $baseY
                        $rz = $baseZ
                        Write-Host ("HOST FOUND! Jimmy x={0:N2} y={1:N2} z={2:N2} | packets received={3}" -f $rx,$ry,$rz,$received) -ForegroundColor Green
                        $lastPrint = $t.ElapsedMilliseconds
                    }
                }
            } catch [System.Net.Sockets.SocketException] { }
        }
        if ($received -eq 0 -and $t.ElapsedMilliseconds - $lastWarn -ge 7000) {
            Write-Host ("Waiting for HOST reply... sent {0}; close other FAKE GUEST windows; start Bully as host" -f $sent) -ForegroundColor Yellow
            $lastWarn = $t.ElapsedMilliseconds
        }
        [System.Threading.Thread]::Sleep(100)
    }
} catch {
    Write-Host ("FAKE GUEST ERROR: " + $_.Exception.Message) -ForegroundColor Red
    exit 1
} finally {
    if ($null -ne $udp) { $udp.Close() }
}
