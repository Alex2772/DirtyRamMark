# Signs the driver with a throw-away self-signed code signing certificate. Test signing mode requires a signature
# (the certificate doesn't have to be trusted).
param([Parameter(Mandatory)] [string] $File)
$ErrorActionPreference = 'Stop'
$cert = New-SelfSignedCertificate -Type CodeSigningCert -Subject 'CN=DirtyRamMark Test' -CertStoreLocation Cert:\CurrentUser\My `
    -NotAfter (Get-Date).AddYears(10)
try {
    $result = Set-AuthenticodeSignature -FilePath $File -Certificate $cert -HashAlgorithm SHA256
    if (-not $result.SignerCertificate) { throw "signing failed: $($result.StatusMessage)" }
} finally {
    Remove-Item -Path "Cert:\CurrentUser\My\$($cert.Thumbprint)" -ErrorAction SilentlyContinue
}
