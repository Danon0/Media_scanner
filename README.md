# Media Scanner
## Сборка

```bash
mkdir build && cd build
cmake ..
make

## Запуск
./media_scanner

## Настройка интервала и каталога
./media_scanner -i 30 -d /home/user/Music

## HTTP-режим
./media_scanner --http

### После запуска JSON доступен по адресу: http://localhost:1234/media_files. Для завершения нажмите Ctrl+C.
