# Jackal Box

Микро-DAW / драм-машина для себя: удобный степ-секвенсор с кнопками как у железных драм-машин, нарезка брейков, запись гитары через ASIO, хостинг VST3 и встроенные «цифровые шакалы» (bitcrush, codec, glitch).

Статус: этап 1 из 5: скелет приложения, выбор аудиодрайвера, мониторинг гитары с показом задержки, метроном.

## Что внутри будет

- Драм-машина 8 строк × 16 кнопок, p-locks, ратчеты, вероятность, филл, своя длина строки.
- Брейк-дорожка: слайсинг amen и любых петель, включая гитару.
- Глитч-пэды и кнопка «Мутация».
- ШАКАЛ-цепь на каждой дорожке и мастере.
- Гитара через ASIO: петли длиной в паттерн, запись сухая.
- VST3-инструменты и эффекты, MIDI-вход и запись в шаги.
- Экспорт WAV мастера и стемов.

## Стек

C++20, CMake, [JUCE 8](https://juce.com) (VST3-хостинг, ASIO), Windows + MSVC.

## Лицензии зависимостей

- JUCE 8: AGPLv3 или коммерческая.
- VST3 SDK: MIT (с версии 3.8).
- ASIO SDK: GPLv3 или проприетарная от Steinberg.


## Сборка

Windows (VS 2022 или новее, CMake сам найдёт установленную студию):

```
cmake -B build -A x64
cmake --build build --config Release
```

ASIO включается, если указать путь к ASIO SDK от Steinberg: `-DJACKAL_ASIO_SDK_DIR=C:/sdk/asiosdk_2.3.3` или переменная окружения `ASIO_SDK_DIR`. Без SDK приложение работает через Windows Audio / DirectSound.

Только движок и тесты (Linux или Windows, без JUCE):

```
cmake -B build -DJACKAL_BUILD_APP=OFF
cmake --build build
ctest --test-dir build --output-on-failure
```

Настройки хранятся в `%APPDATA%\JackalBox\JackalBox.xml`.

## Лицензия

AGPLv3, см. [LICENSE](LICENSE).
