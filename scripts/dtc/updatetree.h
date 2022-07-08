#ifndef _UPDATETREE_H
#define _UPDATETREE_H

#include "dtc.h"

#define PRINTF_RED	"\033[0;32;31m"
#define PRINTF_YELLOW	"\033[1;33m"
#define PRINTF_NONE	"\033[m"

/* add for sunxi sys_config parser*/
const char *of_prop_next_string(struct property *prop, const char *cur);
int of_prop_string_count(const char *prop_val, int prop_len);
int sunxi_gpio_to_name(int port, int port_num, char *name);
int sunxi_get_propval(struct node *node, const char *name);


#define of_property_for_each_string(prop, s)		\
	for (s = of_prop_next_string(prop, NULL);	\
	    s;						\
	    s = of_prop_next_string(prop, s))

#define for_each_section_in_list(secions, sec_list)	\
	for (sec_list = list_first(&secions);		\
	    sec_list;					\
	    sec_list = list_next(&secions, sec_list))

#define for_each_entry_in_section(entry, o)		\
	for (o = list_first(&entry);			\
	    o;						\
	    o = list_next(&entry, o))


int sunxi_gpio_to_name(int port, int port_num, char *name);
int dt_update_source(const char *fexname, FILE *f, struct dt_info *dti);

int process_mainkey(char *mainkey, char parent_name[], char child_name[], int *state);
int sunxi_build_new_node(struct dt_info *dti, char pnode_name[], char node_name[]);
struct node *sunxi_get_node(struct node *tree, const char *string);


cell_t sunxi_dt_add_new_node_to_pinctrl(struct node *pinctrl_node,
					const char *dev_name,
					const char *pname,
					char *gpio_name,
					int gpio_value[],
					struct dt_info *dti);

int sunxi_dt_init_pinconf_prop(struct script_section *section,
				struct dt_info *dti,
				struct node *node,
				int sleep_state);

void create_pinconf_node(const char *section_name,
			   struct dt_info *dti,
			   struct node *node,
			   struct script_entry *ep,
			   struct property *prop);

int insert_pinconf_node(const char *section_name,
			 struct dt_info *dti,
			 struct node *node,
			 struct script_entry *ep,
			 const char *prop_name);

void sunxi_dt_update_pin_group_sleep(const char *section_name,
				  struct dt_info *dti,
				  struct node *node,
				  struct script_entry *ep);
void sunxi_dt_update_pin_group_default(const char *section_name,
				  struct dt_info *dti,
				  struct node *node,
				  struct script_entry *ep);

int sunxi_update_pinconf_node( const char *section_name,
			  const char *prop_name,
			  struct dt_info *dti,
			  struct node *node,
			  struct script_entry *ep,
			  int value[]);

void sunxi_dt_update_gpio_group(struct dt_info *dti,
			   struct node *node,
			   struct script_entry *ep,
			   struct script_gpio_entry *entry);
void sunxi_dt_update_pin_group(const char *section_name,
				  struct dt_info *dti,
				  struct node *node,
				  struct script_entry *ep,
				  int sleep_state);



void sunxi_dt_update_propval_gpio(const char *section_name,
				     struct script_entry *ep,
				     struct node *node,
				     struct dt_info *dti,
				     int sleep_state);

void sunxi_dt_update_propval_string(const char *section_name,
				      struct script_entry *ep,
				      struct node *node);

void sunxi_dt_update_propval_cells(const char *section_name,
				     struct script_entry *ep,
				     struct node *node);

void sunxi_dt_update_propval_empty(const char *section_name,
				      struct script_entry *ep,
				      struct node *node);



/*end for sunxi sys_config parser */
#endif

