set pagination off
set confirm off
handle SIGUSR1 nostop noprint pass
break lvlViewMoveTick
ignore 1 40
commands
silent
set var debug_render_raster=0
set var debug_freeze_processing=0
set var g_DebugMode=0
set var show_debug_menu_flag=0
set var debugCameraStateCounter=10
set var debugCameraPosition.x=4280.09
set var debugCameraPosition.y=428.009
set var debugCameraPosition.z=-10700.2
set var debugCameraHorizontalAngle=180.0
set var debugCameraVerticalAngle=351.47
set var g_CurrentPlayer->pos=debugCameraPosition
set var g_CurrentPlayer->field_488.pos=debugCameraPosition
printf "BDD_CAMERA: original Dam reservoir\n"
disable 1
continue
end
break bgDetermineVisibleRooms
commands
silent
if debug_render_raster == 0
set var g_BgCurrentRoom=35
set var g_BgPortals[0].offset_portal=0
end
continue
end
run
