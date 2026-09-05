param(
    [Parameter(Mandatory = $true)]
    [string]$Source,

    [Parameter(Mandatory = $true)]
    [string]$EntryPoint,

    [Parameter(Mandatory = $true)]
    [ValidateSet('vs_4_0', 'ps_4_0')]
    [string]$Target,

    [Parameter(Mandatory = $true)]
    [string]$Symbol,

    [Parameter(Mandatory = $true)]
    [string]$Output
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# This is an offline maintainer tool. It uses the D3D compiler shipped with
# supported Windows installations and commits only the resulting DXBC byte
# array; OpenRC itself never loads or distributes d3dcompiler_47.dll.
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;

[ComImport]
[Guid("8BA5FB08-5195-40e2-AC58-0D989C3A0102")]
[InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
public interface ID3DBlob
{
    [PreserveSig]
    IntPtr GetBufferPointer();

    [PreserveSig]
    UIntPtr GetBufferSize();
}

public static class OpenRcD3dCompiler
{
    private const uint EnableStrictness = 1U << 11;
    private const uint OptimizationLevel3 = 1U << 15;
    private const uint WarningsAreErrors = 1U << 18;

    [DllImport("d3dcompiler_47.dll", CharSet = CharSet.Ansi)]
    private static extern int D3DCompile(
        IntPtr sourceData,
        UIntPtr sourceDataSize,
        string sourceName,
        IntPtr defines,
        IntPtr include,
        string entryPoint,
        string target,
        uint flags1,
        uint flags2,
        [MarshalAs(UnmanagedType.Interface)] out ID3DBlob code,
        [MarshalAs(UnmanagedType.Interface)] out ID3DBlob errors);

    private static byte[] CopyBlob(ID3DBlob blob)
    {
        if (blob == null)
        {
            return Array.Empty<byte>();
        }
        ulong unsignedSize = blob.GetBufferSize().ToUInt64();
        if (unsignedSize > Int32.MaxValue)
        {
            throw new InvalidOperationException("D3D compiler blob is too large");
        }
        byte[] result = new byte[(int)unsignedSize];
        if (result.Length != 0)
        {
            Marshal.Copy(blob.GetBufferPointer(), result, 0, result.Length);
        }
        return result;
    }

    public static byte[] Compile(
        byte[] source,
        string sourceName,
        string entryPoint,
        string target)
    {
        GCHandle sourceHandle = default(GCHandle);
        ID3DBlob code = null;
        ID3DBlob errors = null;
        try
        {
            sourceHandle = GCHandle.Alloc(source, GCHandleType.Pinned);
            int result = D3DCompile(
                sourceHandle.AddrOfPinnedObject(),
                new UIntPtr((uint)source.Length),
                sourceName,
                IntPtr.Zero,
                IntPtr.Zero,
                entryPoint,
                target,
                EnableStrictness | OptimizationLevel3 | WarningsAreErrors,
                0U,
                out code,
                out errors);
            if (result < 0)
            {
                string diagnostic = "D3DCompile failed with HRESULT 0x" +
                    result.ToString("X8");
                byte[] errorBytes = CopyBlob(errors);
                if (errorBytes.Length != 0)
                {
                    diagnostic += ": " +
                        System.Text.Encoding.UTF8.GetString(errorBytes).TrimEnd('\0', '\r', '\n');
                }
                throw new InvalidOperationException(diagnostic);
            }
            return CopyBlob(code);
        }
        finally
        {
            if (sourceHandle.IsAllocated)
            {
                sourceHandle.Free();
            }
            if (code != null)
            {
                Marshal.ReleaseComObject(code);
            }
            if (errors != null)
            {
                Marshal.ReleaseComObject(errors);
            }
        }
    }
}
'@

$sourcePath = (Resolve-Path -LiteralPath $Source).Path
$outputPath = [System.IO.Path]::GetFullPath($Output)
$sourceText = [System.IO.File]::ReadAllText($sourcePath)
$sourceBytes = [System.Text.UTF8Encoding]::new($false).GetBytes($sourceText)
$compiled = [OpenRcD3dCompiler]::Compile(
    $sourceBytes,
    [System.IO.Path]::GetFileName($sourcePath),
    $EntryPoint,
    $Target)

$lines = [System.Collections.Generic.List[string]]::new()
$lines.Add('#pragma once')
$lines.Add('')
$lines.Add('// Generated deterministically from ' +
    [System.IO.Path]::GetFileName($sourcePath) + ' (' + $EntryPoint +
    ', ' + $Target + ') by scripts/compile-d3d11-shader.ps1.')
$lines.Add('inline constexpr unsigned char ' + $Symbol + '[] = {')
for ($offset = 0; $offset -lt $compiled.Length; $offset += 12) {
    $last = [Math]::Min($offset + 12, $compiled.Length)
    $items = [System.Collections.Generic.List[string]]::new()
    for ($index = $offset; $index -lt $last; ++$index) {
        $items.Add(('0x{0:x2}' -f $compiled[$index]))
    }
    $suffix = if ($last -lt $compiled.Length) { ',' } else { '' }
    $lines.Add('    ' + [string]::Join(', ', $items) + $suffix)
}
$lines.Add('};')
$lines.Add('')

$parent = [System.IO.Path]::GetDirectoryName($outputPath)
if (![string]::IsNullOrEmpty($parent)) {
    [System.IO.Directory]::CreateDirectory($parent) | Out-Null
}
$content = [string]::Join("`n", $lines)
[System.IO.File]::WriteAllText(
    $outputPath,
    $content,
    [System.Text.UTF8Encoding]::new($false))
