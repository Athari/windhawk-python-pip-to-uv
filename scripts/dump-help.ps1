$PythonVersion = 312
$PythonPath = "C:\Apps\Python\Python$PythonVersion"

$env:PATH = "$PythonPath;$PythonPath\Scripts;$env:PATH"
$env:PYTHONUTF8 = "1"
$ErrorActionPreference = 'Stop'

Push-Location (Join-Path (Split-Path $MyInvocation.MyCommand.Path) "out")

try {
    $sep = "`n`n------`n`n"

    function Get-Cmds($text) {
        $cmds = @()
        $in = $false
        foreach ($line in $text -split "`n") {
            if ($line -match '^\s*Commands:\s*$') {
                $in = $true
                continue
            } elseif ($in -and $line -match '^\s*$') {
                break
            } elseif ($in -and $line -match '^\s*([a-z\-]+)\s') {
                $cmds += $matches[1]
            }
        }
        return $cmds
    }

    function Dump($parts) {
        $cmd = $parts -join ' '
        $file = ($parts -join '-' -replace "python--m-", "") + ".md"
        $help = (Invoke-Expression "$cmd -h" 2>&1 | Out-String).Trim()
        Set-Content $file "> $cmd -h`n`n$help"

        Get-Cmds $help | foreach {
            $sub = $parts + $_
            $subCmd = $sub -join ' '
            $subHelp = Invoke-Expression "$subCmd -h" 2>&1 | Out-String
            Add-Content $file "$sep> $subCmd -h`n`n$subHelp"
            Dump $sub
        }
    }

    Dump @('uv')
    Dump @('python', '-m', 'pip')
    Dump @('python', '-m', 'venv')
    Dump @('python', '-m', 'virtualenv')

} catch { throw } finally {
    Pop-Location
}