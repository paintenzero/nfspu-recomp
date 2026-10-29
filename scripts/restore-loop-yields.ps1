# Restore scheduling points missing from the older checked-in lift. The current
# pcrecomp generator emits the same hook before every direct backward branch.
[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
$taskRoot=Split-Path $PSScriptRoot -Parent
$taskUtf8=[Text.UTF8Encoding]::new($false)
$taskTotal=0
foreach($taskDir in @('gen','dx7z')) {
    foreach($taskFile in Get-ChildItem -LiteralPath (Join-Path $taskRoot "src/recomp/$taskDir") -Filter 'recomp_*.c') {
        if($taskFile.Name -eq 'recomp_dispatch.c'){continue}
        $taskText=[IO.File]::ReadAllText($taskFile.FullName)
        $taskCount=[ref]0
        $taskPattern='(?m)^(?<hook>[ \t]*RECOMP_BACKEDGE\(\);\r?\n)?(?<line>[ \t]*(?:if[^\r\n]*|goto )\bgoto L_(?<target>[0-9A-Fa-f]{8});[^\r\n]*/\* 0x(?<address>[0-9A-Fa-f]{8}): (?:j[a-z]+|loop[a-z]*) [^\r\n]*\*/[^\r\n]*|[ \t]*goto L_(?<target2>[0-9A-Fa-f]{8});[^\r\n]*/\* 0x(?<address2>[0-9A-Fa-f]{8}): jmp [^\r\n]*\*/[^\r\n]*)'
        $taskUpdated=[regex]::Replace($taskText,$taskPattern,[Text.RegularExpressions.MatchEvaluator]{param($m)
            if($m.Groups['hook'].Success){return $m.Value}
            $target=if($m.Groups['target'].Success){$m.Groups['target'].Value}else{$m.Groups['target2'].Value}
            $address=if($m.Groups['address'].Success){$m.Groups['address'].Value}else{$m.Groups['address2'].Value}
            if([Convert]::ToUInt32($target,16) -gt [Convert]::ToUInt32($address,16)){return $m.Value}
            $taskCount.Value++
            return '    RECOMP_BACKEDGE();'+"`n"+$m.Value
        })
        if($taskUpdated -cne $taskText){[IO.File]::WriteAllText($taskFile.FullName,$taskUpdated,$taskUtf8)}
        $taskTotal+=$taskCount.Value
    }
}
Write-Host "Restored $taskTotal loop scheduling points"
