param(
    [ValidateSet('Standalone', 'REAPER')][string]$App = 'Standalone',
    [switch]$DialogOnOpen
)

$ErrorActionPreference = 'Stop'
try {
    $taskProcessName = if ($App -eq 'REAPER') { 'reaper' } else { 'TONE3000' }
    $taskExecutable = if ($App -eq 'REAPER') {
        Join-Path $env:ProgramFiles 'REAPER (x64)\reaper.exe'
    } else {
        Join-Path $env:ProgramFiles 'TONE3000\TONE3000.exe'
    }
    if (!(Test-Path -LiteralPath $taskExecutable)) { throw "$App was not found at $taskExecutable" }
    if (Get-Process -Name $taskProcessName -ErrorAction SilentlyContinue) {
        throw "Close $App, then open this shortcut again. It needs a new process to enable dialog previews."
    }
    $taskPreviousPreview = $env:T3K_DIALOG_PREVIEW
    $taskPreviousOnOpen = $env:T3K_DIALOG_PREVIEW_ON_OPEN
    try {
        $env:T3K_DIALOG_PREVIEW = '1'
        $env:T3K_DIALOG_PREVIEW_ON_OPEN = if ($DialogOnOpen) { '1' } else { '0' }
        # The user is opening the interactive app to test its dialogs.
        Start-Process -FilePath $taskExecutable -WindowStyle Normal
    } finally {
        $env:T3K_DIALOG_PREVIEW = $taskPreviousPreview
        $env:T3K_DIALOG_PREVIEW_ON_OPEN = $taskPreviousOnOpen
    }
} catch {
    # Desktop shortcuts hide the helper console; keep failures readable by NVDA.
    Add-Type -AssemblyName PresentationFramework
    [System.Windows.MessageBox]::Show($_.Exception.Message, 'TONE3000 dialog test', 'OK', 'Information') | Out-Null
    exit 1
}
