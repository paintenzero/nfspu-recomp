# Reapply application-specific adapters after regenerating the lift.
# Exact anchors deliberately fail if a new binary/lift needs re-analysis.
[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
$taskRoot=Split-Path $PSScriptRoot -Parent
$taskUtf8=[Text.UTF8Encoding]::new($false)
function Patch-Lift([string]$Relative,[string]$Anchor,[string]$Replacement,[string]$Marker) {
    $taskPath=Join-Path $taskRoot $Relative
    $taskText=[IO.File]::ReadAllText($taskPath)
    if($taskText.Contains($Marker)){return}
    if(($taskText.Split(@($Anchor),[StringSplitOptions]::None).Count-1) -ne 1){throw "Widescreen hook anchor changed in $Relative"}
    [IO.File]::WriteAllText($taskPath,$taskText.Replace($Anchor,$Replacement),$taskUtf8)
}
$taskAnchor='    RECOMP_ENTER(0x00467FC0u);'
Patch-Lift 'src/recomp/gen/recomp_0005.c' $taskAnchor ("    if (recomp_mode_hook) recomp_mode_hook(g_cur_func, esp); /* NFSPU race mode adapter */`n"+$taskAnchor) '/* NFSPU race mode adapter */'
$taskAnchor='    RECOMP_ENTER(0x005391B0u);'
Patch-Lift 'src/recomp/gen/recomp_0018.c' $taskAnchor ("    uint32_t _nfspu_camera = ecx; /* NFSPU camera capture */`n"+$taskAnchor) '/* NFSPU camera capture */'
$taskAnchor='    RECOMP_FLAGS_OUT(); esp += 20; return; /* 0x005392C3: ret 0x10 */'
Patch-Lift 'src/recomp/gen/recomp_0018.c' $taskAnchor ("    if (recomp_projection_hook) recomp_projection_hook(_nfspu_camera); /* NFSPU Hor+ adapter */`n"+$taskAnchor) '/* NFSPU Hor+ adapter */'
$taskAnchor='    RECOMP_FLAGS_OUT(); esp += 4; return; /* 0x0041FEC0: ret  */'
Patch-Lift 'src/recomp/gen/recomp_0001.c' $taskAnchor ("    { extern void nfspu_hud_layout_apply(void); nfspu_hud_layout_apply(); } /* NFSPU HUD arrangement */`n"+$taskAnchor) '/* NFSPU HUD arrangement */'
