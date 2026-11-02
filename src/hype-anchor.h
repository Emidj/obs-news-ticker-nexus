/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <graphics/matrix4.h>
/* Preserve the visible ticker baseline when its optional upper canvas changes.
 * Only scene-item position is corrected; scale, rotation, bounds and crop stay
 * owned by OBS. Apply synchronously after the video update releases its lock,
 * so no frame is drawn at an intermediate position and no UI task outlives
 * its source or module. */
struct hype_anchor_item {obs_sceneitem_t *item;struct vec2 point,pos;};
struct hype_anchor_request {struct ticker *ticker;obs_source_t *source;struct hype_anchor_item *items;size_t count;float old_y;};
static float ticker_top_inset(const struct ticker *t)
{return t->style==STYLE_BASIC?0:t->style==STYLE_NEWS?5:compact_style(t->style)?1:reference_top_inset(t->style,(float)t->width,(float)t->upper_height);}
static float ticker_baseline(const struct ticker *t)
{return t->hype_enabled?(float)t->hype_height:ticker_top_inset(t);}
static bool collect_anchor_item(obs_scene_t *scene,obs_sceneitem_t *item,void *data)
{
 UNUSED_PARAMETER(scene);struct hype_anchor_request *r=data;
 if(obs_sceneitem_is_group(item))obs_scene_enum_items(obs_sceneitem_group_get_scene(item),collect_anchor_item,data);
 if(obs_sceneitem_get_source(item)!=r->source)return true;
 for(size_t i=0;i<r->count;i++)if(r->items[i].item==item)return true;
 obs_sceneitem_force_update_transform(item);struct matrix4 m;obs_sceneitem_get_draw_transform(item,&m);
 r->items=brealloc(r->items,(r->count+1)*sizeof(*r->items));struct hype_anchor_item *row=&r->items[r->count++];row->item=item;obs_sceneitem_addref(item);obs_sceneitem_get_pos(item,&row->pos);
 vec2_set(&row->point,m.t.x+m.y.x*r->old_y,m.t.y+m.y.y*r->old_y);return true;
}
static bool collect_anchor_scene(void *data,obs_source_t *source)
{obs_scene_t *scene=obs_scene_from_source(source);if(scene)obs_scene_enum_items(scene,collect_anchor_item,data);return true;}
static void apply_hype_anchor(void *data)
{
 struct hype_anchor_request *r=data;struct ticker *t=r->ticker;
 AcquireSRWLockShared(&t->lock);float y=ticker_baseline(t);ReleaseSRWLockShared(&t->lock);
 for(size_t i=0;i<r->count;i++){
  struct hype_anchor_item *row=&r->items[i];obs_sceneitem_t *item=row->item;
  if(obs_sceneitem_get_scene(item)&&obs_sceneitem_get_source(item)==r->source){
   struct obs_transform_info info;obs_sceneitem_get_info2(item,&info);
   // Respect a user drag performed while the update was queued.
   if(fabsf(info.pos.x-row->pos.x)<.01f&&fabsf(info.pos.y-row->pos.y)<.01f){
    obs_sceneitem_set_info2(item,&info);obs_sceneitem_force_update_transform(item);
    struct matrix4 m;obs_sceneitem_get_draw_transform(item,&m);
    info.pos.x+=row->point.x-(m.t.x+m.y.x*y);info.pos.y+=row->point.y-(m.t.y+m.y.y*y);
    obs_sceneitem_set_pos(item,&info.pos);obs_sceneitem_force_update_transform(item);
   }
  }
  obs_sceneitem_release(item);
 }
 bfree(r->items);obs_source_release(r->source);bfree(r);
}
static struct hype_anchor_request *begin_hype_anchor(struct ticker *t,obs_data_t *settings)
{
 bool enabled=obs_data_get_bool(settings,"hype_enabled");unsigned h=bounded(obs_data_get_int(settings,"hype_height"),16,200);
 AcquireSRWLockShared(&t->lock);
 bool change=t->width&&(enabled!=t->hype_enabled||(enabled&&h!=t->hype_height));
 if(!change){ReleaseSRWLockShared(&t->lock);return NULL;}
 struct hype_anchor_request *r=bzalloc(sizeof(*r));r->ticker=t;r->source=obs_source_get_ref(t->self);r->old_y=ticker_baseline(t);
 ReleaseSRWLockShared(&t->lock);obs_enum_scenes(collect_anchor_scene,r);return r;
}
static void finish_hype_anchor(struct ticker *t,struct hype_anchor_request *r)
{
 UNUSED_PARAMETER(t);if(!r)return;
 if(r->count)apply_hype_anchor(r);
 else{bfree(r->items);obs_source_release(r->source);bfree(r);}
}
