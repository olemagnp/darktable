#ifdef HAVE_CONFIG_H
  #include "config.h"
#endif
#include <stdlib.h>
#include <math.h>
#include <assert.h>
#include <string.h>
#include "iop/equalizer.h"
#include "gui/histogram.h"
#include "develop/develop.h"
#include "control/control.h"
#include "gui/gtk.h"

#include "iop/equalizer_eaw.h"

#define DT_GUI_EQUALIZER_INSET 5
#define DT_GUI_CURVE_INFL .3f

#ifndef M_PI
# define M_PI		3.14159265358979323846	/* pi */
#endif

void process (struct dt_iop_module_t *self, dt_dev_pixelpipe_iop_t *piece, void *i, void *o, int x, int y, float scale, int width, int height)
{
  float *in = (float *)i;
  float *out = (float *)o;
  memcpy(out, in, 3*sizeof(float)*width*height);
#if 1
  dt_iop_equalizer_data_t *d = (dt_iop_equalizer_data_t *)(piece->data);

  // 1 pixel in this buffer represents 1.0/scale pixels in original image:
  // so finest possible level here is
  // 1 pixel: level 1
  // 2 pixels: level 2
  // 4 pixels: level 3
  const float l1 = 1.0f + log2f(piece->iscale/scale);                      // finest level
  float lm = 0; for(int k=MIN(width,height)*piece->iscale/scale;k;k>>=1) lm++; // coarsest level
  // printf("level range in %d %d: %f %f\n", 1, d->num_levels, l1, lm);
  // level 1 => full resolution
  int numl = 0; for(int k=MIN(width,height);k;k>>=1) numl++;

  // TODO: fixed alloc for data piece at capped resolution.
  float **tmp = (float **)malloc(sizeof(float *)*numl);
  for(int k=1;k<numl;k++)
  {
    const int wd = (int)(1 + (width>>(k-1))), ht = (int)(1 + (height>>(k-1)));
    tmp[k] = (float *)malloc(sizeof(float)*wd*ht);
    // printf("level %d with %d X %d\n", k, wd, ht);
  }

  for(int level=1;level<numl;level++) dt_iop_equalizer_wtf(out, tmp, level, width, height);
  // for(int level=1;level<numl;level++) dt_iop_equalizer_wtf(out, tmp, level, width, height, ch);
  for(int l=1;l<numl;l++)
  {
    for(int ch=0;ch<3;ch++)
    {
      // 1 => 1
      // 0 => num_levels
      // coefficients in range [0, 2], 1 being neutral.
      const float coeff = 2*(dt_draw_curve_calc_value(d->curve[ch], 1.0-((lm-l1)*(l-1)/(float)(numl-1) + l1)/(float)d->num_levels));
      // printf("level %d coeff %f\n", l, coeff);
      // printf("level %d => l: %f => x: %f\n", l, (lm-l1)*(l-1)/(float)(numl-1) + l1, 1.0-((lm-l1)*(l-1)/(float)(numl-1) + l1)/(float)d->num_levels);
      const int step = 1<<l;
      for(int j=0;j<height;j+=step)      for(int i=step/2;i<width;i+=step) out[3*width*j + 3*i + ch] *= coeff;
      for(int j=step/2;j<height;j+=step) for(int i=0;i<width;i+=step)      out[3*width*j + 3*i + ch] *= coeff;
      for(int j=step/2;j<height;j+=step) for(int i=step/2;i<width;i+=step) out[3*width*j + 3*i + ch] *= coeff*coeff;
    }
  }
  for(int level=numl-1;level>0;level--) dt_iop_equalizer_iwtf(out, tmp, level, width, height);
  // for(int level=numl-1;level>0;level--) dt_iop_equalizer_iwtf(out, tmp, level, width, height, ch);

  for(int k=1;k<numl;k++) free(tmp[k]);
  free(tmp);
#endif
}

void commit_params (struct dt_iop_module_t *self, dt_iop_params_t *p1, dt_dev_pixelpipe_t *pipe, dt_dev_pixelpipe_iop_t *piece)
{
  // pull in new params to gegl
  dt_iop_equalizer_data_t *d = (dt_iop_equalizer_data_t *)(piece->data);
  dt_iop_equalizer_params_t *p = (dt_iop_equalizer_params_t *)p1;
#ifdef HAVE_GEGL
  // TODO
#else
  for(int ch=0;ch<3;ch++) for(int k=0;k<DT_IOP_EQUALIZER_BANDS;k++)
    dt_draw_curve_set_point(d->curve[ch], k, p->equalizer_x[ch][k], p->equalizer_y[ch][k]);
  int l = 0; for(int k=(int)MIN(pipe->iwidth*pipe->iscale,pipe->iheight*pipe->iscale);k;k>>=1) l++;
  d->num_levels = l;
#endif
}

void init_pipe (struct dt_iop_module_t *self, dt_dev_pixelpipe_t *pipe, dt_dev_pixelpipe_iop_t *piece)
{
  // create part of the gegl pipeline
  dt_iop_equalizer_data_t *d = (dt_iop_equalizer_data_t *)malloc(sizeof(dt_iop_equalizer_data_t));
  dt_iop_equalizer_params_t *default_params = (dt_iop_equalizer_params_t *)self->default_params;
  piece->data = (void *)d;
  for(int ch=0;ch<3;ch++)
  {
    d->curve[ch] = dt_draw_curve_new(0.0, 1.0);
    for(int k=0;k<DT_IOP_EQUALIZER_BANDS;k++)
      (void)dt_draw_curve_add_point(d->curve[ch], default_params->equalizer_x[ch][k], default_params->equalizer_y[ch][k]);
  }
#ifdef HAVE_GEGL
  #error "gegl version not implemeted!"
  piece->input = piece->output = gegl_node_new_child(pipe->gegl, "operation", "gegl:dt-contrast-curve", "sampling-points", 65535, "curve", d->curve[0], NULL);
#else
  d->num_levels = 1;
#endif
}

void cleanup_pipe (struct dt_iop_module_t *self, dt_dev_pixelpipe_t *pipe, dt_dev_pixelpipe_iop_t *piece)
{
  // clean up everything again.
#ifdef HAVE_GEGL
#error "gegl version not implemented!"
  // (void)gegl_node_remove_child(pipe->gegl, piece->input);
#endif
  dt_iop_equalizer_data_t *d = (dt_iop_equalizer_data_t *)(piece->data);
  for(int ch=0;ch<3;ch++) dt_draw_curve_destroy(d->curve[ch]);
  free(piece->data);
}

void gui_update(struct dt_iop_module_t *self)
{
  // nothing to do, gui curve is read directly from params during expose event.
  gtk_widget_queue_draw(self->widget);
}

void init(dt_iop_module_t *module)
{
  module->params = malloc(sizeof(dt_iop_equalizer_params_t));
  module->default_params = malloc(sizeof(dt_iop_equalizer_params_t));
  module->params_size = sizeof(dt_iop_equalizer_params_t);
  module->gui_data = NULL;
  dt_iop_equalizer_params_t tmp;
  for(int ch=0;ch<3;ch++)
  {
    for(int k=0;k<DT_IOP_EQUALIZER_BANDS;k++) tmp.equalizer_x[ch][k] = k/(float)(DT_IOP_EQUALIZER_BANDS-1);
    for(int k=0;k<DT_IOP_EQUALIZER_BANDS;k++) tmp.equalizer_y[ch][k] = 0.5f;
  }
  memcpy(module->params, &tmp, sizeof(dt_iop_equalizer_params_t));
  memcpy(module->default_params, &tmp, sizeof(dt_iop_equalizer_params_t));
}

void cleanup(dt_iop_module_t *module)
{
  free(module->gui_data);
  module->gui_data = NULL;
  free(module->params);
  module->params = NULL;
}

void gui_init(struct dt_iop_module_t *self)
{
  self->gui_data = malloc(sizeof(dt_iop_equalizer_gui_data_t));
  dt_iop_equalizer_gui_data_t *c = (dt_iop_equalizer_gui_data_t *)self->gui_data;
  dt_iop_equalizer_params_t *p = (dt_iop_equalizer_params_t *)self->params;

  c->channel = DT_IOP_EQUALIZER_Y;
  int ch = (int)c->channel;
  c->minmax_curve = dt_draw_curve_new(0.0, 1.0);
  for(int k=0;k<DT_IOP_EQUALIZER_BANDS;k++) (void)dt_draw_curve_add_point(c->minmax_curve, p->equalizer_x[ch][k], p->equalizer_y[ch][k]);
  c->mouse_x = c->mouse_y = c->mouse_pick = -1.0;
  c->dragging = 0;
  c->mouse_radius = 0.1f;
  self->widget = GTK_WIDGET(gtk_vbox_new(FALSE, 0));
  c->area = GTK_DRAWING_AREA(gtk_drawing_area_new());
  gtk_box_pack_start(GTK_BOX(self->widget), GTK_WIDGET(c->area), TRUE, TRUE, 0);
  gtk_drawing_area_size(c->area, 195, 195);

  gtk_widget_add_events(GTK_WIDGET(c->area), GDK_POINTER_MOTION_MASK | GDK_POINTER_MOTION_HINT_MASK | GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_LEAVE_NOTIFY_MASK);
  g_signal_connect (G_OBJECT (c->area), "expose-event",
                    G_CALLBACK (dt_iop_equalizer_expose), self);
  g_signal_connect (G_OBJECT (c->area), "button-press-event",
                    G_CALLBACK (dt_iop_equalizer_button_press), self);
  g_signal_connect (G_OBJECT (c->area), "button-release-event",
                    G_CALLBACK (dt_iop_equalizer_button_release), self);
  g_signal_connect (G_OBJECT (c->area), "motion-notify-event",
                    G_CALLBACK (dt_iop_equalizer_motion_notify), self);
  g_signal_connect (G_OBJECT (c->area), "leave-notify-event",
                    G_CALLBACK (dt_iop_equalizer_leave_notify), self);
  g_signal_connect (G_OBJECT (c->area), "scroll-event",
                    G_CALLBACK (dt_iop_equalizer_scrolled), self);
  // init gtk stuff
  c->hbox = GTK_HBOX(gtk_hbox_new(FALSE, 0));
  gtk_box_pack_start(GTK_BOX(self->widget), GTK_WIDGET(c->hbox), FALSE, FALSE, 0);

  c->channel_button[0] = GTK_RADIO_BUTTON(gtk_radio_button_new_with_label(NULL, "Y"));
  c->channel_button[1] = GTK_RADIO_BUTTON(gtk_radio_button_new_with_label_from_widget(c->channel_button[0], "Cb"));
  c->channel_button[2] = GTK_RADIO_BUTTON(gtk_radio_button_new_with_label_from_widget(c->channel_button[0], "Cr"));

  g_signal_connect (G_OBJECT (c->channel_button[0]), "toggled",
                    G_CALLBACK (dt_iop_equalizer_button_toggled), self);
  g_signal_connect (G_OBJECT (c->channel_button[1]), "toggled",
                    G_CALLBACK (dt_iop_equalizer_button_toggled), self);
  g_signal_connect (G_OBJECT (c->channel_button[2]), "toggled",
                    G_CALLBACK (dt_iop_equalizer_button_toggled), self);

  for(int k=2;k>=0;k--)
    gtk_box_pack_end(GTK_BOX(c->hbox), GTK_WIDGET(c->channel_button[k]), FALSE, FALSE, 5);
}

void gui_cleanup(struct dt_iop_module_t *self)
{
  dt_iop_equalizer_gui_data_t *c = (dt_iop_equalizer_gui_data_t *)self->gui_data;
  dt_draw_curve_destroy(c->minmax_curve);
  free(self->gui_data);
  self->gui_data = NULL;
}

gboolean dt_iop_equalizer_leave_notify(GtkWidget *widget, GdkEventCrossing *event, gpointer user_data)
{
  dt_iop_module_t *self = (dt_iop_module_t *)user_data;
  dt_iop_equalizer_gui_data_t *c = (dt_iop_equalizer_gui_data_t *)self->gui_data;
  // c->mouse_radius = 1.0/DT_IOP_EQUALIZER_BANDS;
  if(!c->dragging) c->mouse_x = c->mouse_y = -1.0;
  gtk_widget_queue_draw(widget);
  return TRUE;
}

// fills in new parameters based on mouse position (in 0,1)
void dt_iop_equalizer_get_params(dt_iop_equalizer_params_t *p, const int ch, const double mouse_x, const double mouse_y, const float rad)
{
  for(int k=0;k<DT_IOP_EQUALIZER_BANDS;k++)
  {
    const float f = powf(fmaxf(0.0, rad*rad - (mouse_x - p->equalizer_x[ch][k])*(mouse_x - p->equalizer_x[ch][k]))/(rad*rad), 0.25);
    p->equalizer_y[ch][k] = (1-f)*p->equalizer_y[ch][k] + f*mouse_y;
  }
}

gboolean dt_iop_equalizer_expose(GtkWidget *widget, GdkEventExpose *event, gpointer user_data)
{
  dt_iop_module_t *self = (dt_iop_module_t *)user_data;
  dt_iop_equalizer_gui_data_t *c = (dt_iop_equalizer_gui_data_t *)self->gui_data;
  dt_iop_equalizer_params_t p = *(dt_iop_equalizer_params_t *)self->params;
  int ch = (int)c->channel;
  for(int k=0;k<DT_IOP_EQUALIZER_BANDS;k++) dt_draw_curve_set_point(c->minmax_curve, k, p.equalizer_x[ch][k], p.equalizer_y[ch][k]);
  const int inset = DT_GUI_EQUALIZER_INSET;
  int width = widget->allocation.width, height = widget->allocation.height;
  cairo_surface_t *cst = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
  cairo_t *cr = cairo_create(cst);
  // clear bg
  cairo_set_source_rgb (cr, .2, .2, .2);
  cairo_paint(cr);

  cairo_translate(cr, inset, inset);
  width -= 2*inset; height -= 2*inset;

  cairo_set_line_width(cr, 1.0);
  cairo_set_source_rgb (cr, .1, .1, .1);
  cairo_rectangle(cr, 0, 0, width, height);
  cairo_stroke(cr);

  cairo_set_source_rgb (cr, .3, .3, .3);
  cairo_rectangle(cr, 0, 0, width, height);
  cairo_fill(cr);

  if(c->mouse_y > 0 || c->dragging)
  {
    // draw min/max curves:
    dt_iop_equalizer_get_params(&p, c->channel, c->mouse_x, 1., c->mouse_radius);
    for(int k=0;k<DT_IOP_EQUALIZER_BANDS;k++)
      dt_draw_curve_set_point(c->minmax_curve, k, p.equalizer_x[ch][k], p.equalizer_y[ch][k]);
    dt_draw_curve_calc_values(c->minmax_curve, 0.0, 1.0, DT_IOP_EQUALIZER_RES, c->draw_min_xs, c->draw_min_ys);

    p = *(dt_iop_equalizer_params_t *)self->params;
    dt_iop_equalizer_get_params(&p, c->channel, c->mouse_x, .0, c->mouse_radius);
    for(int k=0;k<DT_IOP_EQUALIZER_BANDS;k++)
      dt_draw_curve_set_point(c->minmax_curve, k, p.equalizer_x[ch][k], p.equalizer_y[ch][k]);
    dt_draw_curve_calc_values(c->minmax_curve, 0.0, 1.0, DT_IOP_EQUALIZER_RES, c->draw_max_xs, c->draw_max_ys);
  }

  // draw grid
  cairo_set_line_width(cr, .4);
  cairo_set_source_rgb (cr, .1, .1, .1);
  dt_draw_grid(cr, 8, width, height);
  
  // draw selected cursor
  cairo_set_line_width(cr, 1.);
  cairo_translate(cr, 0, height);

  // TODO: draw frequency histogram in bg.
#if 0
  // draw lum h istogram in background
  dt_develop_t *dev = darktable.develop;
  float *hist, hist_max;
  hist = dev->histogram_pre;
  hist_max = dev->histogram_pre_max;
  if(hist_max > 0)
  {
    cairo_save(cr);
    cairo_scale(cr, width/63.0, -(height-5)/(float)hist_max);
    cairo_set_source_rgba(cr, .2, .2, .2, 0.5);
    dt_gui_histogram_draw_8(cr, hist, 3);
    cairo_restore(cr);
  }
#endif
 
  cairo_set_operator(cr, CAIRO_OPERATOR_ADD);
  cairo_set_line_width(cr, 2.);
  for(int i=0;i<3;i++)
  { // draw curves, selected last.
    int ch = ((int)c->channel+i+1)%3;
    switch(ch)
    {
      case DT_IOP_EQUALIZER_Y:
        cairo_set_source_rgba(cr, .6, .6, .6, .2);
        break;
      case DT_IOP_EQUALIZER_Cb:
        cairo_set_source_rgba(cr, .0, .0, 1., .2);
        break;
      default: //case DT_IOP_EQUALIZER_Cr:
        cairo_set_source_rgba(cr, 1., .0, .0, .2);
        break;
    }
    p = *(dt_iop_equalizer_params_t *)self->params;
    for(int k=0;k<DT_IOP_EQUALIZER_BANDS;k++)
      dt_draw_curve_set_point(c->minmax_curve, k, p.equalizer_x[ch][k], p.equalizer_y[ch][k]);
    dt_draw_curve_calc_values(c->minmax_curve, 0.0, 1.0, DT_IOP_EQUALIZER_RES, c->draw_xs, c->draw_ys);
    // cairo_set_line_cap  (cr, CAIRO_LINE_CAP_SQUARE);
    cairo_move_to(cr, 0, 0);
    for(int k=0;k<DT_IOP_EQUALIZER_RES;k++) cairo_line_to(cr, k*width/(float)(DT_IOP_EQUALIZER_RES-1), - height*c->draw_ys[k]);
    cairo_line_to(cr, width, 0);
    cairo_close_path(cr);
    cairo_stroke_preserve(cr);
    cairo_fill(cr);
  }

  if(c->mouse_y > 0 || c->dragging)
  { // draw min/max, if selected
    // cairo_set_source_rgba(cr, .6, .6, .6, .5);
    cairo_move_to(cr, 0, - height*c->draw_min_ys[0]);
    for(int k=1;k<DT_IOP_EQUALIZER_RES;k++)    cairo_line_to(cr, k*width/(float)(DT_IOP_EQUALIZER_RES-1), - height*c->draw_min_ys[k]);
    for(int k=DT_IOP_EQUALIZER_RES-2;k>=0;k--) cairo_line_to(cr, k*width/(float)(DT_IOP_EQUALIZER_RES-1), - height*c->draw_max_ys[k]);
    cairo_close_path(cr);
    cairo_fill(cr);
    // draw mouse focus circle
    cairo_set_source_rgba(cr, .9, .9, .9, .5);
    const float pos = DT_IOP_EQUALIZER_RES * c->mouse_x;
    int k = (int)pos; const float f = k - pos;
    if(k >= DT_IOP_EQUALIZER_RES-1) k = DT_IOP_EQUALIZER_RES - 2;
    float ht = -height*(f*c->draw_ys[k] + (1-f)*c->draw_ys[k+1]);
    cairo_arc(cr, c->mouse_x*width, ht, c->mouse_radius*width, 0, 2.*M_PI);
    cairo_stroke(cr);
  }

  cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);

  cairo_destroy(cr);
  cairo_t *cr_pixmap = gdk_cairo_create(gtk_widget_get_window(widget));
  cairo_set_source_surface (cr_pixmap, cst, 0, 0);
  cairo_paint(cr_pixmap);
  cairo_destroy(cr_pixmap);
  cairo_surface_destroy(cst);
  return TRUE;
}

gboolean dt_iop_equalizer_motion_notify(GtkWidget *widget, GdkEventMotion *event, gpointer user_data)
{
  dt_iop_module_t *self = (dt_iop_module_t *)user_data;
  dt_iop_equalizer_gui_data_t *c = (dt_iop_equalizer_gui_data_t *)self->gui_data;
  dt_iop_equalizer_params_t *p = (dt_iop_equalizer_params_t *)self->params;
  const int inset = DT_GUI_EQUALIZER_INSET;
  int height = widget->allocation.height - 2*inset, width = widget->allocation.width - 2*inset;
  if(!c->dragging) c->mouse_x = CLAMP(event->x - inset, 0, width)/(float)width;
  c->mouse_y = 1.0 - CLAMP(event->y - inset, 0, height)/(float)height;
  if(c->dragging)
  {
    *p = c->drag_params;
    dt_iop_equalizer_get_params(p, c->channel, c->mouse_x, c->mouse_y + c->mouse_pick, c->mouse_radius);
    dt_dev_add_history_item(darktable.develop, self);
  }
  gtk_widget_queue_draw(widget);
  gint x, y;
  gdk_window_get_pointer(event->window, &x, &y, NULL);
  return TRUE;
}

gboolean dt_iop_equalizer_button_press(GtkWidget *widget, GdkEventButton *event, gpointer user_data)
{ // set active point
  dt_iop_module_t *self = (dt_iop_module_t *)user_data;
  dt_iop_equalizer_gui_data_t *c = (dt_iop_equalizer_gui_data_t *)self->gui_data;
  c->drag_params = *(dt_iop_equalizer_params_t *)self->params;
  const int inset = DT_GUI_EQUALIZER_INSET;
  int height = widget->allocation.height - 2*inset, width = widget->allocation.width - 2*inset;
  c->mouse_pick = dt_draw_curve_calc_value(c->minmax_curve, CLAMP(event->x - inset, 0, width)/(float)width);
  c->mouse_pick -= 1.0 - CLAMP(event->y - inset, 0, height)/(float)height;
  c->dragging = 1;
  return TRUE;
}

gboolean dt_iop_equalizer_button_release(GtkWidget *widget, GdkEventButton *event, gpointer user_data)
{
  dt_iop_module_t *self = (dt_iop_module_t *)user_data;
  dt_iop_equalizer_gui_data_t *c = (dt_iop_equalizer_gui_data_t *)self->gui_data;
  c->dragging = 0;
  return TRUE;
}

gboolean dt_iop_equalizer_scrolled(GtkWidget *widget, GdkEventScroll *event, gpointer user_data)
{
  dt_iop_module_t *self = (dt_iop_module_t *)user_data;
  dt_iop_equalizer_gui_data_t *c = (dt_iop_equalizer_gui_data_t *)self->gui_data;
  if(event->direction == GDK_SCROLL_UP   && c->mouse_radius > 1.0/DT_IOP_EQUALIZER_BANDS) c->mouse_radius *= 0.7;
  if(event->direction == GDK_SCROLL_DOWN && c->mouse_radius < 1.0) c->mouse_radius *= 1.42;
  gtk_widget_queue_draw(widget);
  return TRUE;
}

void dt_iop_equalizer_button_toggled(GtkToggleButton *togglebutton, gpointer user_data)
{
  dt_iop_module_t *self = (dt_iop_module_t *)user_data;
  dt_iop_equalizer_gui_data_t *c = (dt_iop_equalizer_gui_data_t *)self->gui_data;
  if(gtk_toggle_button_get_active(togglebutton))
  {
    for(int k=0;k<3;k++) if(c->channel_button[k] == GTK_RADIO_BUTTON(togglebutton))
    {
      c->channel = (dt_iop_equalizer_channel_t)k;
      return;
    }
  }
}

