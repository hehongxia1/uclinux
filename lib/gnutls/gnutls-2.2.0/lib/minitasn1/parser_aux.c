/*
 *      Copyright (C) 2004, 2006, 2007 Free Software Foundation
 *      Copyright (C) 2000,2001 Fabio Fiorina
 *
 * This file is part of LIBTASN1.
 *
 * The LIBTASN1 library is free software; you can redistribute it
 * and/or modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA
 */

#include <int.h>
#include <errors.h>
#include "parser_aux.h"
#include "gstr.h"
#include "structure.h"
#include "element.h"

char _asn1_identifierMissing[MAX_NAME_SIZE + 1];	/* identifier name not found */

/***********************************************/
/* Type: list_type                             */
/* Description: type used in the list during   */
/* the structure creation.                     */
/***********************************************/
typedef struct list_struct
{
  node_asn *node;
  struct list_struct *next;
} list_type;


/* Pointer to the first element of the list */
list_type *firstElement = NULL;

/******************************************************/
/* Function : _asn1_add_node                          */
/* Description: creates a new NODE_ASN element and    */
/* puts it in the list pointed by firstElement.       */
/* Parameters:                                        */
/*   type: type of the new element (see TYPE_         */
/*         and CONST_ constants).                     */
/* Return: pointer to the new element.                */
/******************************************************/
node_asn *
_asn1_add_node (unsigned int type)
{
  list_type *listElement;
  node_asn *punt;

  punt = (node_asn *) _asn1_calloc (1, sizeof (node_asn));
  if (punt == NULL)
    return NULL;

  listElement = (list_type *) _asn1_malloc (sizeof (list_type));
  if (listElement == NULL)
    {
      _asn1_free (punt);
      return NULL;
    }

  listElement->node = punt;
  listElement->next = firstElement;
  firstElement = listElement;

  punt->type = type;

  return punt;
}

/**
 * asn1_find_node:
 * @pointer: NODE_ASN element pointer.
 * @name: null terminated string with the element's name to find.
 *
 * Searches for an element called NAME starting from POINTER.  The
 * name is composed by differents identifiers separated by dots.  When
 * *POINTER has a name, the first identifier must be the name of
 * *POINTER, otherwise it must be the name of one child of *POINTER.
 *
 * Return value: the searching result. NULL if not found.
 **/
ASN1_TYPE
asn1_find_node (ASN1_TYPE pointer, const char *name)
{
  node_asn *p;
  char *n_end, n[MAX_NAME_SIZE + 1];
  const char *n_start;

  if (pointer == NULL)
    return NULL;

  if (name == NULL)
    return NULL;

  p = pointer;
  n_start = name;

  if (p->name != NULL)
    {				/* has *pointer got a name ? */
      n_end = strchr (n_start, '.');	/* search the first dot */
      if (n_end)
	{
	  memcpy (n, n_start, n_end - n_start);
	  n[n_end - n_start] = 0;
	  n_start = n_end;
	  n_start++;
	}
      else
	{
	  _asn1_str_cpy (n, sizeof (n), n_start);
	  n_start = NULL;
	}

      while (p)
	{
	  if ((p->name) && (!strcmp (p->name, n)))
	    break;
	  else
	    p = p->right;
	}			/* while */

      if (p == NULL)
	return NULL;
    }
  else
    {				/* *pointer doesn't have a name */
      if (n_start[0] == 0)
	return p;
    }

  while (n_start)
    {				/* Has the end of NAME been reached? */
      n_end = strchr (n_start, '.');	/* search the next dot */
      if (n_end)
	{
	  memcpy (n, n_start, n_end - n_start);
	  n[n_end - n_start] = 0;
	  n_start = n_end;
	  n_start++;
	}
      else
	{
	  _asn1_str_cpy (n, sizeof (n), n_start);
	  n_start = NULL;
	}

      if (p->down == NULL)
	return NULL;

      p = p->down;

      /* The identifier "?LAST" indicates the last element 
         in the right chain. */
      if (!strcmp (n, "?LAST"))
	{
	  if (p == NULL)
	    return NULL;
	  while (p->right)
	    p = p->right;
	}
      else
	{			/* no "?LAST" */
	  while (p)
	    {
	      if ((p->name) && (!strcmp (p->name, n)))
		break;
	      else
		p = p->right;
	    }
	  if (p == NULL)
	    return NULL;
	}
    }				/* while */

  return p;
}


/******************************************************************/
/* Function : _asn1_set_value                                     */
/* Description: sets the field VALUE in a NODE_ASN element. The   */
/*              previous value (if exist) will be lost            */
/* Parameters:                                                    */
/*   node: element pointer.                                       */
/*   value: pointer to the value that you want to set.            */
/*   len: character number of value.                              */
/* Return: pointer to the NODE_ASN element.                       */
/******************************************************************/
node_asn *
_asn1_set_value (node_asn * node, const void *_value, unsigned int len)
{
  const unsigned char *value = _value;

  if (node == NULL)
    return node;
  if (node->value)
    {
      _asn1_free (node->value);
      node->value = NULL;
      node->value_len = 0;
    }
  if (!len)
    return node;
  node->value = (unsigned char *) _asn1_malloc (len);
  if (node->value == NULL)
    return NULL;
  node->value_len = len;

  memcpy (node->value, value, len);
  return node;
}

/******************************************************************/
/* Function : _asn1_set_name                                      */
/* Description: sets the field NAME in a NODE_ASN element. The    */
/*              previous value (if exist) will be lost            */
/* Parameters:                                                    */
/*   node: element pointer.                                       */
/*   name: a null terminated string with the name that you want   */
/*         to set.                                                */
/* Return: pointer to the NODE_ASN element.                       */
/******************************************************************/
node_asn *
_asn1_set_name (node_asn * node, const char *name)
{
  if (node == NULL)
    return node;

  if (node->name)
    {
      _asn1_free (node->name);
      node->name = NULL;
    }

  if (name == NULL)
    return node;

  if (strlen (name))
    {
      node->name = (char *) _asn1_strdup (name);
      if (node->name == NULL)
	return NULL;
    }
  else
    node->name = NULL;
  return node;
}

/******************************************************************/
/* Function : _asn1_set_right                                     */
/* Description: sets the field RIGHT in a NODE_ASN element.       */
/* Parameters:                                                    */
/*   node: element pointer.                                       */
/*   right: pointer to a NODE_ASN element that you want be pointed*/
/*          by NODE.                                              */
/* Return: pointer to *NODE.                                      */
/******************************************************************/
node_asn *
_asn1_set_right (node_asn * node, node_asn * right)
{
  if (node == NULL)
    return node;
  node->right = right;
  if (right)
    right->left = node;
  return node;
}

/******************************************************************/
/* Function : _asn1_get_right                                     */
/* Description: returns the element pointed by the RIGHT field of */
/*              a NODE_ASN element.                               */
/* Parameters:                                                    */
/*   node: NODE_ASN element pointer.                              */
/* Return: field RIGHT of NODE.                                   */
/******************************************************************/
node_asn *
_asn1_get_right (node_asn * node)
{
  if (node == NULL)
    return NULL;
  return node->right;
}

/******************************************************************/
/* Function : _asn1_get_last_right                                */
/* Description: return the last element along the right chain.    */
/* Parameters:                                                    */
/*   node: starting element pointer.                              */
/* Return: pointer to the last element along the right chain.     */
/******************************************************************/
node_asn *
_asn1_get_last_right (node_asn * node)
{
  node_asn *p;

  if (node == NULL)
    return NULL;
  p = node;
  while (p->right)
    p = p->right;
  return p;
}

/******************************************************************/
/* Function : _asn1_set_down                                      */
/* Description: sets the field DOWN in a NODE_ASN element.        */
/* Parameters:                                                    */
/*   node: element pointer.                                       */
/*   down: pointer to a NODE_ASN element that you want be pointed */
/*          by NODE.                                              */
/* Return: pointer to *NODE.                                      */
/******************************************************************/
node_asn *
_asn1_set_down (node_asn * node, node_asn * down)
{
  if (node == NULL)
    return node;
  node->down = down;
  if (down)
    down->left = node;
  return node;
}

/******************************************************************/
/* Function : _asn1_get_down                                      */
/* Description: returns the element pointed by the DOWN field of  */
/*              a NODE_ASN element.                               */
/* Parameters:                                                    */
/*   node: NODE_ASN element pointer.                              */
/* Return: field DOWN of NODE.                                    */
/******************************************************************/
node_asn *
_asn1_get_down (node_asn * node)
{
  if (node == NULL)
    return NULL;
  return node->down;
}

/******************************************************************/
/* Function : _asn1_get_name                                      */
/* Description: returns the name of a NODE_ASN element.           */
/* Parameters:                                                    */
/*   node: NODE_ASN element pointer.                              */
/* Return: a null terminated string.                              */
/******************************************************************/
char *
_asn1_get_name (node_asn * node)
{
  if (node == NULL)
    return NULL;
  return node->name;
}

/******************************************************************/
/* Function : _asn1_mod_type                                      */
/* Description: change the field TYPE of an NODE_ASN element.     */
/*              The new value is the old one | (bitwise or) the   */
/*              paramener VALUE.                                  */
/* Parameters:                                                    */
/*   node: NODE_ASN element pointer.                              */
/*   value: the integer value that must be or-ed with the current */
/*          value of field TYPE.                                  */
/* Return: NODE pointer.                                          */
/******************************************************************/
node_asn *
_asn1_mod_type (node_asn * node, unsigned int value)
{
  if (node == NULL)
    return node;
  node->type |= value;
  return node;
}


/******************************************************************/
/* Function : _asn1_remove_node                                   */
/* Description: gets free the memory allocated for an NODE_ASN    */
/*              element (not the elements pointed by it).         */
/* Parameters:                                                    */
/*   node: NODE_ASN element pointer.                              */
/******************************************************************/
void
_asn1_remove_node (node_asn * node)
{
  if (node == NULL)
    return;

  if (node->name != NULL)
    _asn1_free (node->name);
  if (node->value != NULL)
    _asn1_free (node->value);
  _asn1_free (node);
}

/******************************************************************/
/* Function : _asn1_find_up                                       */
/* Description: return the father of the NODE_ASN element.        */
/* Parameters:                                                    */
/*   node: NODE_ASN element pointer.                              */
/* Return: Null if not found.                                     */
/******************************************************************/
node_asn *
_asn1_find_up (node_asn * node)
{
  node_asn *p;

  if (node == NULL)
    return NULL;

  p = node;

  while ((p->left != NULL) && (p->left->right == p))
    p = p->left;

  return p->left;
}

/******************************************************************/
/* Function : _asn1_delete_list                                   */
/* Description: deletes the list elements (not the elements       */
/*  pointed by them).                                             */
/******************************************************************/
void
_asn1_delete_list (void)
{
  list_type *listElement;

  while (firstElement)
    {
      listElement = firstElement;
      firstElement = firstElement->next;
      _asn1_free (listElement);
    }
}

/******************************************************************/
/* Function : _asn1_delete_list_and nodes                         */
/* Description: deletes the list elements and the elements        */
/*  pointed by them.                                              */
/******************************************************************/
void
_asn1_delete_list_and_nodes (void)
{
  list_type *listElement;

  while (firstElement)
    {
      listElement = firstElement;
      firstElement = firstElement->next;
      _asn1_remove_node (listElement->node);
      _asn1_free (listElement);
    }
}


char *
_asn1_ltostr (long v, char *str)
{
  long d, r;
  char temp[20];
  int count, k, start;

  if (v < 0)
    {
      str[0] = '-';
      start = 1;
      v = -v;
    }
  else
    start = 0;

  count = 0;
  do
    {
      d = v / 10;
      r = v - d * 10;
      temp[start + count] = '0' + (char) r;
      count++;
      v = d;
    }
  while (v);

  for (k = 0; k < count; k++)
    str[k + start] = temp[start + count - k - 1];
  str[count + start] = 0;
  return str;
}


/******************************************************************/
/* Function : _asn1_change_integer_value                          */
/* Description: converts into DER coding the value assign to an   */
/*   INTEGER constant.                                            */
/* Parameters:                                                    */
/*   node: root of an ASN1element.                                */
/* Return:                                                        */
/*   ASN1_ELEMENT_NOT_FOUND if NODE is NULL,                       */
/*   otherwise ASN1_SUCCESS                                             */
/******************************************************************/
asn1_retCode
_asn1_change_integer_value (ASN1_TYPE node)
{
  node_asn *p;
  unsigned char val[SIZEOF_UNSIGNED_LONG_INT];
  unsigned char val2[SIZEOF_UNSIGNED_LONG_INT + 1];
  int len;

  if (node == NULL)
    return ASN1_ELEMENT_NOT_FOUND;

  p = node;
  while (p)
    {
      if ((type_field (p->type) == TYPE_INTEGER) && (p->type & CONST_ASSIGN))
	{
	  if (p->value)
	    {
	      _asn1_convert_integer (p->value, val, sizeof (val), &len);
	      asn1_octet_der (val, len, val2, &len);
	      _asn1_set_value (p, val2, len);
	    }
	}

      if (p->down)
	{
	  p = p->down;
	}
      else
	{
	  if (p == node)
	    p = NULL;
	  else if (p->right)
	    p = p->right;
	  else
	    {
	      while (1)
		{
		  p = _asn1_find_up (p);
		  if (p == node)
		    {
		      p = NULL;
		      break;
		    }
		  if (p->right)
		    {
		      p = p->right;
		      break;
		    }
		}
	    }
	}
    }

  return ASN1_SUCCESS;
}


/******************************************************************/
/* Function : _asn1_expand_object_id                              */
/* Description: expand the IDs of an OBJECT IDENTIFIER constant.  */
/* Parameters:                                                    */
/*   node: root of an ASN1 element.                               */
/* Return:                                                        */
/*   ASN1_ELEMENT_NOT_FOUND if NODE is NULL,                       */
/*   otherwise ASN1_SUCCESS                                             */
/******************************************************************/
asn1_retCode
_asn1_expand_object_id (ASN1_TYPE node)
{
  node_asn *p, *p2, *p3, *p4, *p5;
  char name_root[MAX_NAME_SIZE], name2[2 * MAX_NAME_SIZE + 1];
  int move, tlen;

  if (node == NULL)
    return ASN1_ELEMENT_NOT_FOUND;

  _asn1_str_cpy (name_root, sizeof (name_root), node->name);

  p = node;
  move = DOWN;

  while (!((p == node) && (move == UP)))
    {
      if (move != UP)
	{
	  if ((type_field (p->type) == TYPE_OBJECT_ID)
	      && (p->type & CONST_ASSIGN))
	    {
	      p2 = p->down;
	      if (p2 && (type_field (p2->type) == TYPE_CONSTANT))
		{
		  if (p2->value && !isdigit (p2->value[0]))
		    {
		      _asn1_str_cpy (name2, sizeof (name2), name_root);
		      _asn1_str_cat (name2, sizeof (name2), ".");
		      _asn1_str_cat (name2, sizeof (name2), p2->value);
		      p3 = asn1_find_node (node, name2);
		      if (!p3 || (type_field (p3->type) != TYPE_OBJECT_ID) ||
			  !(p3->type & CONST_ASSIGN))
			return ASN1_ELEMENT_NOT_FOUND;
		      _asn1_set_down (p, p2->right);
		      _asn1_remove_node (p2);
		      p2 = p;
		      p4 = p3->down;
		      while (p4)
			{
			  if (type_field (p4->type) == TYPE_CONSTANT)
			    {
			      p5 = _asn1_add_node_only (TYPE_CONSTANT);
			      _asn1_set_name (p5, p4->name);
			      tlen = strlen (p4->value);
			      if (tlen > 0)
				_asn1_set_value (p5, p4->value, tlen + 1);
			      if (p2 == p)
				{
				  _asn1_set_right (p5, p->down);
				  _asn1_set_down (p, p5);
				}
			      else
				{
				  _asn1_set_right (p5, p2->right);
				  _asn1_set_right (p2, p5);
				}
			      p2 = p5;
			    }
			  p4 = p4->right;
			}
		      move = DOWN;
		      continue;
		    }
		}
	    }
	  move = DOWN;
	}
      else
	move = RIGHT;

      if (move == DOWN)
	{
	  if (p->down)
	    p = p->down;
	  else
	    move = RIGHT;
	}

      if (p == node)
	{
	  move = UP;
	  continue;
	}

      if (move == RIGHT)
	{
	  if (p->right)
	    p = p->right;
	  else
	    move = UP;
	}
      if (move == UP)
	p = _asn1_find_up (p);
    }


  /*******************************/
  /*       expand DEFAULT        */
  /*******************************/
  p = node;
  move = DOWN;

  while (!((p == node) && (move == UP)))
    {
      if (move != UP)
	{
	  if ((type_field (p->type) == TYPE_OBJECT_ID) &&
	      (p->type & CONST_DEFAULT))
	    {
	      p2 = p->down;
	      if (p2 && (type_field (p2->type) == TYPE_DEFAULT))
		{
		  _asn1_str_cpy (name2, sizeof (name2), name_root);
		  _asn1_str_cat (name2, sizeof (name2), ".");
		  _asn1_str_cat (name2, sizeof (name2), p2->value);
		  p3 = asn1_find_node (node, name2);
		  if (!p3 || (type_field (p3->type) != TYPE_OBJECT_ID) ||
		      !(p3->type & CONST_ASSIGN))
		    return ASN1_ELEMENT_NOT_FOUND;
		  p4 = p3->down;
		  name2[0] = 0;
		  while (p4)
		    {
		      if (type_field (p4->type) == TYPE_CONSTANT)
			{
			  if (name2[0])
			    _asn1_str_cat (name2, sizeof (name2), ".");
			  _asn1_str_cat (name2, sizeof (name2), p4->value);
			}
		      p4 = p4->right;
		    }
		  tlen = strlen (name2);
		  if (tlen > 0)
		    _asn1_set_value (p2, name2, tlen + 1);
		}
	    }
	  move = DOWN;
	}
      else
	move = RIGHT;

      if (move == DOWN)
	{
	  if (p->down)
	    p = p->down;
	  else
	    move = RIGHT;
	}

      if (p == node)
	{
	  move = UP;
	  continue;
	}

      if (move == RIGHT)
	{
	  if (p->right)
	    p = p->right;
	  else
	    move = UP;
	}
      if (move == UP)
	p = _asn1_find_up (p);
    }

  return ASN1_SUCCESS;
}


/******************************************************************/
/* Function : _asn1_type_set_config                               */
/* Description: sets the CONST_SET and CONST_NOT_USED properties  */
/*   in the fields of the SET elements.                           */
/* Parameters:                                                    */
/*   node: root of an ASN1 element.                               */
/* Return:                                                        */
/*   ASN1_ELEMENT_NOT_FOUND if NODE is NULL,                       */
/*   otherwise ASN1_SUCCESS                                             */
/******************************************************************/
asn1_retCode
_asn1_type_set_config (ASN1_TYPE node)
{
  node_asn *p, *p2;
  int move;

  if (node == NULL)
    return ASN1_ELEMENT_NOT_FOUND;

  p = node;
  move = DOWN;

  while (!((p == node) && (move == UP)))
    {
      if (move != UP)
	{
	  if (type_field (p->type) == TYPE_SET)
	    {
	      p2 = p->down;
	      while (p2)
		{
		  if (type_field (p2->type) != TYPE_TAG)
		    p2->type |= CONST_SET | CONST_NOT_USED;
		  p2 = p2->right;
		}
	    }
	  move = DOWN;
	}
      else
	move = RIGHT;

      if (move == DOWN)
	{
	  if (p->down)
	    p = p->down;
	  else
	    move = RIGHT;
	}

      if (p == node)
	{
	  move = UP;
	  continue;
	}

      if (move == RIGHT)
	{
	  if (p->right)
	    p = p->right;
	  else
	    move = UP;
	}
      if (move == UP)
	p = _asn1_find_up (p);
    }

  return ASN1_SUCCESS;
}


/******************************************************************/
/* Function : _asn1_check_identifier                              */
/* Description: checks the definitions of all the identifiers     */
/*   and the first element of an OBJECT_ID (e.g. {pkix 0 4}).     */
/*   The _asn1_identifierMissing global variable is filled if     */
/*   necessary.                                                   */
/* Parameters:                                                    */
/*   node: root of an ASN1 element.                               */
/* Return:                                                        */
/*   ASN1_ELEMENT_NOT_FOUND      if NODE is NULL,                 */
/*   ASN1_IDENTIFIER_NOT_FOUND   if an identifier is not defined, */
/*   otherwise ASN1_SUCCESS                                       */
/******************************************************************/
asn1_retCode
_asn1_check_identifier (ASN1_TYPE node)
{
  node_asn *p, *p2;
  char name2[MAX_NAME_SIZE * 2 + 2];

  if (node == NULL)
    return ASN1_ELEMENT_NOT_FOUND;

  p = node;
  while (p)
    {
      if (type_field (p->type) == TYPE_IDENTIFIER)
	{
	  _asn1_str_cpy (name2, sizeof (name2), node->name);
	  _asn1_str_cat (name2, sizeof (name2), ".");
	  _asn1_str_cat (name2, sizeof (name2), p->value);
	  p2 = asn1_find_node (node, name2);
	  if (p2 == NULL)
	    {
	      strcpy (_asn1_identifierMissing, p->value);
	      return ASN1_IDENTIFIER_NOT_FOUND;
	    }
	}
      else if ((type_field (p->type) == TYPE_OBJECT_ID) &&
	       (p->type & CONST_DEFAULT))
	{
	  p2 = p->down;
	  if (p2 && (type_field (p2->type) == TYPE_DEFAULT))
	    {
	      _asn1_str_cpy (name2, sizeof (name2), node->name);
	      _asn1_str_cat (name2, sizeof (name2), ".");
	      _asn1_str_cat (name2, sizeof (name2), p2->value);
	      strcpy (_asn1_identifierMissing, p2->value);
	      p2 = asn1_find_node (node, name2);
	      if (!p2 || (type_field (p2->type) != TYPE_OBJECT_ID) ||
		  !(p2->type & CONST_ASSIGN))
		return ASN1_IDENTIFIER_NOT_FOUND;
	      else
		_asn1_identifierMissing[0] = 0;
	    }
	}
      else if ((type_field (p->type) == TYPE_OBJECT_ID) &&
	       (p->type & CONST_ASSIGN))
	{
	  p2 = p->down;
	  if (p2 && (type_field (p2->type) == TYPE_CONSTANT))
	    {
	      if (p2->value && !isdigit (p2->value[0]))
		{
		  _asn1_str_cpy (name2, sizeof (name2), node->name);
		  _asn1_str_cat (name2, sizeof (name2), ".");
		  _asn1_str_cat (name2, sizeof (name2), p2->value);
		  strcpy (_asn1_identifierMissing, p2->value);
		  p2 = asn1_find_node (node, name2);
		  if (!p2 || (type_field (p2->type) != TYPE_OBJECT_ID) ||
		      !(p2->type & CONST_ASSIGN))
		    return ASN1_IDENTIFIER_NOT_FOUND;
		  else
		    _asn1_identifierMissing[0] = 0;
		}
	    }
	}

      if (p->down)
	{
	  p = p->down;
	}
      else if (p->right)
	p = p->right;
      else
	{
	  while (1)
	    {
	      p = _asn1_find_up (p);
	      if (p == node)
		{
		  p = NULL;
		  break;
		}
	      if (p->right)
		{
		  p = p->right;
		  break;
		}
	    }
	}
    }

  return ASN1_SUCCESS;
}


/******************************************************************/
/* Function : _asn1_set_default_tag                               */
/* Description: sets the default IMPLICIT or EXPLICIT property in */
/*   the tagged elements that don't have this declaration.        */
/* Parameters:                                                    */
/*   node: pointer to a DEFINITIONS element.                      */
/* Return:                                                        */
/*   ASN1_ELEMENT_NOT_FOUND if NODE is NULL or not a pointer to   */
/*     a DEFINITIONS element,                                     */
/*   otherwise ASN1_SUCCESS                                       */
/******************************************************************/
asn1_retCode
_asn1_set_default_tag (ASN1_TYPE node)
{
  node_asn *p;

  if ((node == NULL) || (type_field (node->type) != TYPE_DEFINITIONS))
    return ASN1_ELEMENT_NOT_FOUND;

  p = node;
  while (p)
    {
      if ((type_field (p->type) == TYPE_TAG) &&
	  !(p->type & CONST_EXPLICIT) && !(p->type & CONST_IMPLICIT))
	{
	  if (node->type & CONST_EXPLICIT)
	    p->type |= CONST_EXPLICIT;
	  else
	    p->type |= CONST_IMPLICIT;
	}

      if (p->down)
	{
	  p = p->down;
	}
      else if (p->right)
	p = p->right;
      else
	{
	  while (1)
	    {
	      p = _asn1_find_up (p);
	      if (p == node)
		{
		  p = NULL;
		  break;
		}
	      if (p->right)
		{
		  p = p->right;
		  break;
		}
	    }
	}
    }

  return ASN1_SUCCESS;
}



static const char *
parse_version_number (const char *s, int *number)
{
  int val = 0;

  if (*s == '0' && isdigit (s[1]))
    return NULL;		/* leading zeros are not allowed */
  for (; isdigit (*s); s++)
    {
      val *= 10;
      val += *s - '0';
    }
  *number = val;
  return val < 0 ? NULL : s;
}

/* The parse version functions were copied from libgcrypt.
 */
static const char *
parse_version_string (const char *s, int *major, int *minor, int *micro)
{
  s = parse_version_number (s, major);
  if (!s || *s != '.')
    return NULL;
  s++;
  s = parse_version_number (s, minor);
  if (!s)
    return NULL;
  if (*s != '.')
    {
      *micro = 0;
      return s;
    }
  s++;
  s = parse_version_number (s, micro);
  if (!s)
    return NULL;
  return s;			/* patchlevel */
}

/**
 * asn1_check_version - check for library version
 * @req_version: Required version number, or NULL.
 *
 * Check that the the version of the library is at minimum the
 * requested one and return the version string; return %NULL if the
 * condition is not satisfied.  If a %NULL is passed to this function,
 * no check is done, but the version string is simply returned.
 *
 * See %LIBTASN1_VERSION for a suitable @req_version string.
 *
 * Return value: Version string of run-time library, or %NULL if the
 *   run-time library does not meet the required version number.
 */
const char *
asn1_check_version (const char *req_version)
{
  const char *ver = LIBTASN1_VERSION;
  int my_major, my_minor, my_micro;
  int rq_major, rq_minor, rq_micro;
  const char *my_plvl, *rq_plvl;

  if (!req_version)
    return ver;

  my_plvl = parse_version_string (ver, &my_major, &my_minor, &my_micro);
  if (!my_plvl)
    return NULL;		/* very strange our own version is bogus */
  rq_plvl = parse_version_string (req_version, &rq_major, &rq_minor,
				  &rq_micro);
  if (!rq_plvl)
    return NULL;		/* req version string is invalid */

  if (my_major > rq_major
      || (my_major == rq_major && my_minor > rq_minor)
      || (my_major == rq_major && my_minor == rq_minor
	  && my_micro > rq_micro)
      || (my_major == rq_major && my_minor == rq_minor
	  && my_micro == rq_micro && strcmp (my_plvl, rq_plvl) >= 0))
    {
      return ver;
    }
  return NULL;
}
