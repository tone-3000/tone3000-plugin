# install-webview2.ps1

$packageName = "Microsoft.Web.WebView2"
$packageVersion = "1.0.1901.177"
$sourceName = "nugetRepository"
$sourceUrl = "https://www.nuget.org/api/v2"

Write-Host "Checking if NuGet provider is registered..."

# Check if the NuGet provider is already registered
$existingSource = Get-PackageSource -Name $sourceName -ErrorAction SilentlyContinue

if (-not $existingSource) {
    Write-Host "Registering NuGet package source..."
    Register-PackageSource -ProviderName NuGet -Name $sourceName -Location $sourceUrl -Force
} else {
    Write-Host "NuGet source already registered."
}

Write-Host "Installing WebView2 version $packageVersion..."

try {
    Install-Package $packageName -RequiredVersion $packageVersion -Scope CurrentUser -Source $sourceName -Force
    Write-Host "`n✅ WebView2 installed successfully."
} catch {
    Write-Host "`n❌ Failed to install WebView2: $($_.Exception.Message)"
    exit 1
}
