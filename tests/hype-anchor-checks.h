#include <graphics/matrix4.h>
static struct vec2 anchor_point(obs_sceneitem_t *item,float y){obs_sceneitem_force_update_transform(item);struct matrix4 m;obs_sceneitem_get_draw_transform(item,&m);struct vec2 p;vec2_set(&p,m.t.x+m.y.x*y,m.t.y+m.y.y*y);return p;}
static void hype_anchor_checks(void)
{
 obs_data_t *settings=obs_data_create();obs_data_set_int(settings,"style",STYLE_NEWS);obs_data_set_int(settings,"width",1920);obs_data_set_int(settings,"upper_height",42);obs_data_set_int(settings,"lower_height",31);obs_data_set_bool(settings,"hype_enabled",false);
 obs_source_t *source=obs_source_create("obs_news_ticker_nexus","Anchor QA source",settings,NULL);obs_source_inc_showing(source);settle();
 obs_scene_t *scene=obs_scene_create("Anchor QA scene");obs_sceneitem_t *items[3];struct vec2 points[3];
 for(int i=0;i<3;i++){
  items[i]=obs_scene_add(scene,source);struct obs_transform_info info={0};vec2_set(&info.pos,100.f+i*500,500.f+i*100);vec2_set(&info.scale,.6f+i*.1f,.6f+i*.1f);info.rot=i==0?0:i==1?17:-12;info.alignment=i==1?OBS_ALIGN_BOTTOM|OBS_ALIGN_RIGHT:OBS_ALIGN_TOP|OBS_ALIGN_LEFT;
  if(i==2){info.bounds_type=OBS_BOUNDS_SCALE_INNER;info.bounds_alignment=OBS_ALIGN_CENTER;vec2_set(&info.bounds,700,180);}
  obs_sceneitem_set_info2(items[i],&info);points[i]=anchor_point(items[i],5);
 }
 const unsigned heights[]={28,60,0};
 for(unsigned step=0;step<3;step++){
  obs_data_set_bool(settings,"hype_enabled",heights[step]!=0);if(heights[step])obs_data_set_int(settings,"hype_height",heights[step]);obs_source_update(source,settings);settle();
  bool fixed=true;for(int i=0;i<3;i++){struct vec2 after=anchor_point(items[i],heights[step]?heights[step]:5);if(fabsf(after.x-points[i].x)>.5f||fabsf(after.y-points[i].y)>.5f){printf("ANCHOR step=%u item=%d before=%.2f,%.2f after=%.2f,%.2f\n",step,i,points[i].x,points[i].y,after.x,after.y);fixed=false;}}
  check(step==0?"enabling Hype grows upward with all ticker baselines fixed":step==1?"resizing Hype preserves rotated scaled and bounded item baselines":"disabling Hype keeps the existing ticker position",fixed);
 }
 obs_source_remove(obs_scene_get_source(scene));obs_scene_release(scene);obs_source_dec_showing(source);obs_source_remove(source);obs_source_release(source);obs_data_release(settings);
}
