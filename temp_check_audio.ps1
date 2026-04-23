$files = Get-ChildItem -Path "TrainingVoiceRecords" -Recurse -Filter "*.wav"
foreach ($f in $files) {
    $reader = New-Object System.IO.BinaryReader([System.IO.File]::OpenRead($f.FullName))
    $reader.BaseStream.Position = 28
    $sampleRate = $reader.ReadInt32()
    $reader.Close()
    Write-Output "$($f.Name): $sampleRate Hz"
}