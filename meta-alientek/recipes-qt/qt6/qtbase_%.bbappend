# 阿尔法：无 GPU，Qt 走 linuxfb + 软件渲染；触摸用内置 evdev
PACKAGECONFIG:append:class-target = " linuxfb fontconfig widgets no-opengl"
PACKAGECONFIG:remove:class-target = " xcb gl gles2 eglfs kms gbm vulkan libinput"
