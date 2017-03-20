#ifndef NM_INI_PARSER_H_
#define NM_INI_PARSER_H_

void *nm_ini_parser_init(const nm_str_t *path);
void nm_ini_parser_dump(void *head);
void nm_ini_parser_free(void *head);
int nm_ini_parser_find(const void *head, const char *section,
                       const char *param, nm_str_t *res);

#endif /* NM_INI_PARSER_H_ */
/* vim:set ts=4 sw=4 fdm=marker: */
