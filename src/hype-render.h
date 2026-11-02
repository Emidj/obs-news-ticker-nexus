/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Optional short Hype Train plate, rendered independently of both ticker bands. */
static void render_hype(struct ticker *t)
{
 if(!t->hype_enabled||t->hype_alpha<=.001f)return;
 float w=t->hype_width?fminf(t->width,t->hype_width):t->width*.65f,h=t->hype_height;
 if(w<1||h<1)return;
 if(!t->hype_render)t->hype_render=gs_texrender_create(GS_RGBA,GS_ZS_NONE);
 gs_texrender_reset(t->hype_render);
 if(!gs_texrender_begin(t->hype_render,(uint32_t)w,(uint32_t)h))return;
 struct vec4 clear;vec4_zero(&clear);gs_clear(GS_CLEAR_COLOR,&clear,0,0);gs_ortho(0,w,0,h,-100,100);
 float badge=w*.265f,cut=fminf(12,h*.32f);
 struct vec4 size,bg,accent;vec4_set(&size,w,h,0,0);
 float ba=(t->hype_bg>>24)/255.f,aa=(t->hype_accent>>24)/255.f;
 vec4_set(&bg,(t->hype_bg&255)/255.f*ba,((t->hype_bg>>8)&255)/255.f*ba,((t->hype_bg>>16)&255)/255.f*ba,ba);
 vec4_set(&accent,(t->hype_accent&255)/255.f*aa,((t->hype_accent>>8)&255)/255.f*aa,((t->hype_accent>>16)&255)/255.f*aa,aa);
 gs_effect_set_vec4(gs_effect_get_param_by_name(t->fade_effect,"hype_size"),&size);
 gs_effect_set_vec4(gs_effect_get_param_by_name(t->fade_effect,"hype_bg"),&bg);
 gs_effect_set_vec4(gs_effect_get_param_by_name(t->fade_effect,"hype_accent"),&accent);
 gs_effect_set_float(gs_effect_get_param_by_name(t->fade_effect,"hype_style"),(float)t->style);
 while(gs_effect_loop(t->fade_effect,"HypePlate"))gs_draw_sprite(NULL,0,(uint32_t)w,(uint32_t)h);
 if(t->style==STYLE_MAP&&t->map_enabled)atlas_map(t,w-45,2,40,h-4,1300,67,200,69);
 if(t->style==STYLE_BUSINESS){chevron_band(w-cut*2,2,h-4,cut,2,shade(t->hype_accent,1.6f));}
 if(t->style==STYLE_SPORTS)slanted_panel(w-cut*2,1,cut*2,h-2,cut,0xff00dfff);
 if(t->style==STYLE_SPORT){slanted_panel(0,1,cut*.6f,h-2,cut*.45f,0xff1515df);slanted_panel(w-cut*1.1f,1,cut,h-2,cut*.7f,0xff1515df);}
 if(t->style==STYLE_DARK){slanted_panel(0,1,cut*.6f,h-2,cut*.45f,t->hype_accent);slanted_panel(w-cut*1.4f,1,cut,h-2,cut*.7f,t->hype_accent);}
 if(t->style==STYLE_RED){rectangle(0,1,3,h-2,t->hype_accent);rectangle(5,1,2,h-2,t->hype_accent);}
 rectangle(cut,1,w-cut*2,1,shade(t->hype_accent,1.4f));
 float edges[]={0,badge,w*.605f,w*.765f,w*.875f,w-cut*.6f};
 for(int i=0;i<5;i++){
  float x=edges[i]+(i?5:cut+4),space=edges[i+1]-x-(i==4?5:7);
  if(i>1)rectangle(edges[i],h*.27f,1,h*.46f,alpha_color(t->hype_fg,.55f));
  centered_text(t,HYPE_BADGE+i,x,0,fmaxf(0,space),h);
 }
 gs_texrender_end(t->hype_render);
 gs_effect_set_texture(gs_effect_get_param_by_name(t->fade_effect,"image"),gs_texrender_get_texture(t->hype_render));
 gs_effect_set_float(gs_effect_get_param_by_name(t->fade_effect,"opacity"),t->hype_alpha);
 gs_matrix_push();gs_matrix_translate3f(0,(1-t->hype_alpha)*4,0);
 while(gs_effect_loop(t->fade_effect,"Draw"))gs_draw_sprite(NULL,0,(uint32_t)w,(uint32_t)h);
 gs_matrix_pop();
}
