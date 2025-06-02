# Wokwi project

This is a Wokwi project. Please edit this README file and add a description of your project.

## Usage

1. Add parts by clicking the blue "+" button in the diagram editor
2. Connect parts by dragging wires between them
3. Click the green play button to start the simulation


```
curl -L https://wokwi.com/ci/install.sh | sh
wokwi-cli --version
export WOKWI_CLI_TOKEN=wok_aK1EaHWh9Mjwde13VdjgmAftfXAU2G3Hcd2c595d
```

```
arduino-cli compile --fqbn arduino:avr:uno MFRC522_chip_simulation.ino --output-dir build
```

# fast start
1. Экспортировать готовый проект в папку src как есть
2. отредактировать названия в makefile и wokwi.toml
3. Вывод custom чипов в vscode отображается на вкладке `Output -> Wokwi chips`