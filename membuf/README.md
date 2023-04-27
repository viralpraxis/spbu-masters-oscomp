## Параметры модуля

### devices_count

общее количество девайсов

`echo 5 | sudo tee /sys/module/membuf/parameters/devices_count`

Макс. кол-во девайсов -- `MAX_DEVICES_COUNT`, начальное -- `INITIAL_DEVICES_COUNT`


### buffer_size_data

Размер буфера для i-го девайса


максимальный размер буфера -- `MAX_BUFFER_SIZE`, начальный -- `INITIAL_BUFFER_SIZE`

пример: (установить размера буфера для /dev/membuf3 в 239 байт)


`echo "3 239" | sudo tee /sys/module/membuf/parameters/buffer_size_data`