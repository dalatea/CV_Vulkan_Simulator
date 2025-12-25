1.	ROS2
  Установка ROS2 Jazzy для Ubuntu Noble 24.04: https://docs.ros.org/en/jazzy/Installation/Ubuntu-Install-Debs.html
2.	Vulkan
  2.1.	Установка драйвера, если нет
  sudo apt install -y mesa-vulkan-drivers
  2.2.	Набор для диагностики, после установки стоит попробовать запустить vkcube, чтобы убедиться, что устройство поддерживает Vulkan. Если выводятся сообщения об ошибках, то стоит убедиться в том, что драйверы обновлены и что используемая графическая карта поддерживается
  sudo apt install vulkan-tools
  2.3.	Загрузка Vulkan loader для линковки с Vulkan API
  sudo apt install -y libvulkan-dev 
  2.4.	Набор инструментов компиляции/валидации шейдеров
  Sudo apt install -y glslang-tools
  2.5.	Набор отладочных слоёв Vulkan, выполняющих динамические проверки корректности использования API
  sudo apt install -y vulkan-validationlayers
3.	Инструменты для работы с бинарным промежуточным языком для графических шейдеров SPIR-V
  sudo apt install spirv-tools
4.	GLFW — кроссплатформенная библиотека для создания окон, обработки ввода и графической подсистемой ОС.
  sudo apt install -y libglfw3-dev
5.	Библиотека линейной алгебры
  sudo apt install -y libglm-dev
6.	Библиотека JSON for Modern C++
  sudo apt install -y nlohmann-json3-dev


Перед запуском произвести настройку среды:
source /opt/ros/jazzy/setup.bash
Запуск без аргументов отображает сцену scene_config.json из папки assets по умолчанию:
./CV_Simulator
Аргументы:
--scene путь к json файлу — передать новый файл сцены
--stress — запуск стресс-теста
--stress
 --stress --stress-count число — стресс-тест с заданным числом объектов на сцене
--stress –stress-spacing число — стресс-тест с заданным расстоянием между объектами
--stress --stress-model путь — стресс-тест с указанием модели для отображения
Примеры:
-	Стресс-тест 
./CV_Simulator --stress --stress-count 200 --stress-spacing 1.2 --stress-model ../models/smooth_vase.obj
-	Запуск программы с передачей пути к конфигурационному файлу
./CV_Simulator –scene ../assets/simple_scene.json
