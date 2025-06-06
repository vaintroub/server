--disable_warnings
drop table if exists t0, t1, t2, t3, t4, t5, t11, t12, t21, t22;
--enable_warnings

set optimizer_switch='semijoin=on,firstmatch=on,loosescan=on,semijoin_with_cache=on';

#
# 1. Subquery with GROUP/HAVING
#
create table t1 (oref int, grp int, ie int) ;
insert into t1 (oref, grp, ie) values
 (1, 1, 1),
 (1, 1, 1),
 (1, 2, NULL),

 (2, 1, 3),

 (3, 1, 4),
 (3, 2, NULL);

# Ok, for
#   select max(ie) from t1 where oref=PARAM group by grp
# we'll have:
# PARAM      subquery result  
#   1   ->   {(1), (NULL)}    matching + NULL
#   2   ->   {(3)}            non-matching
#   3   ->   {(3), (NULL)}    non-matching + NULL
#   4   ->   {}               empty set

create table t2 (oref int, a int);
insert into t2 values 
  (1, 1),
  (2, 2),
  (3, 3),
  (4, NULL),
  (2, NULL);

# true, false, null, false, null
select a, oref, a in (select max(ie) 
  from t1 where oref=t2.oref group by grp) Z from t2;

# This must have a trigcond
explain extended
select a, oref, a in (select max(ie) 
  from t1 where oref=t2.oref group by grp) Z from t2;

# This must not have a trigcond:
explain extended
select a, oref from t2 
where a in (select max(ie) from t1 where oref=t2.oref group by grp);
select a, oref, a in (
  select max(ie) from t1 where oref=t2.oref group by grp union
  select max(ie) from t1 where oref=t2.oref group by grp
  ) Z from t2;

# Non-correlated subquery, 2 NULL evaluations
create table t3 (a int);
insert into t3 values (NULL), (NULL);
flush status;
--disable_ps2_protocol
select a in (select max(ie) from t1 where oref=4 group by grp) from t3;
--enable_ps2_protocol
show status like 'Handler_read_rnd_next';
select ' ^ This must show 11' Z;

set @save_optimizer_switch=@@optimizer_switch;
set @@optimizer_switch="partial_match_rowid_merge=off,partial_match_table_scan=off";

# This must show trigcond:
explain extended select a in (select max(ie) from t1 where oref=4 group by grp) from t3;

set @@optimizer_switch=@save_optimizer_switch;
drop table t1, t2, t3;

# 
#  2. Subquery handled with 'index_subquery':
# 
create table t1 (a int, oref int, key(a));
insert into t1 values 
  (1, 1),
  (1, NULL),
  (2, 3),
  (2, NULL),
  (3, NULL);
insert into t1 values (5, 7), (8, 9), (4, 1); 

create table t2 (a int, oref int);
insert into t2 values (1, 1), (2,2), (NULL, 3), (NULL, 4);

select oref, a, a in (select a from t1 where oref=t2.oref) Z from t2;

# The next explain shows "using index" but that is just incorrect display
# (there is a bug filed about this).
explain extended 
select oref, a, a in (select a from t1 where oref=t2.oref) Z from t2;

--disable_ps2_protocol
flush status;
select oref, a from t2 where a in (select a from t1 where oref=t2.oref);
# This will only show access to t2:
show status like '%Handler_read_rnd_next';

# Check that repeated NULL-scans are not cached (subq. is not correlated):
delete from t2;
insert into t2 values (NULL, 0),(NULL, 0), (NULL, 0), (NULL, 0);

set optimizer_switch='subquery_cache=off';
flush status;
select oref, a, a in (select a from t1 where oref=t2.oref) Z from t2;
show status like '%Handler_read%';
select 'No key lookups, seq reads: 29= 5 reads from t2 + 4 * 6 reads from t1.' Z;
set @@optimizer_switch=@save_optimizer_switch;
--enable_ps2_protocol

drop table t1, t2;

#
# 3. Subquery handled with 'unique_index_subquery':
#
create table t1 (a int, b int, primary key (a));
insert into t1 values (1,1), (3,1),(100,1);

create table t2 (a int, b int);
insert into t2 values (1,1),(2,1),(NULL,1),(NULL,0);

select a,b, a in (select a from t1 where t1.b = t2.b) Z from t2 ;

drop table t1, t2;

#
# 4. Subquery that is a join, with ref access
#
create table t1 (a int, b int, key(a));
insert into t1 values 
  (0,0),(1,1),(2,2),(3,3),(4,4),(5,5),(6,6),(7,7),(8,8),(9,9);

create table t2 like t1;
insert into t2 select * from t1;
update t2 set b=1;

create table t3 (a int, oref int);
insert into t3 values (1, 1), (NULL,1), (NULL,0);
select a, oref, 
       t3.a in (select t1.a from t1, t2 where t1.b=t2.a and t2.b=t3.oref) Z 
from t3;

# This must have trigcond in WHERE and HAVING:
explain extended
select a, oref, 
       t3.a in (select t1.a from t1, t2 where t1.b=t2.a and t2.b=t3.oref) Z 
from t3;

drop table t1, t2, t3;


#
# BUG#24085: Wrong query result for "NULL IN (SELECT ... UNION SELECT ...)"
#

# case 1: NULL IN (SELECT not_null_val FROM ...) w/o HAVING/GROUP-BY/etc
create table t1 (a int NOT NULL, b int NOT NULL, key(a));
insert into t1 values 
  (0,0),(1,1),(2,2),(3,3),(4,4),(5,5),(6,6),(7,7),(8,8),(9,9);

create table t2 like t1;
insert into t2 select * from t1;
update t2 set b=1;

create table t3 (a int, oref int);
insert into t3 values (1, 1), (NULL,1), (NULL,0);
select a, oref, 
       t3.a in (select t1.a from t1, t2 where t1.b=t2.a and t2.b=t3.oref) Z 
from t3;

--echo This must show a trig_cond:
explain extended
select a, oref, 
       t3.a in (select t1.a from t1, t2 where t1.b=t2.a and t2.b=t3.oref) Z 
from t3;
drop table t1,t2,t3;


# case 2: NULL IN (SELECT not_null_val FROM) where SELECT has GROUP BY
create table t1 (oref int, grp int);
insert into t1 (oref, grp) values
 (1, 1),
 (1, 1);

# Ok, for  
#   select count(*) from t1 group by grp having grp=PARAM
# we'll have:
#  PARAM    subuqery result
#    1  ->    {(2)}   
#    2  ->    {} - empty set
create table t2 (oref int, a int);
insert into t2 values 
  (1, NULL),
  (2, NULL);

select a, oref, 
       a in (select count(*) from t1 group by grp having grp=t2.oref) Z from t2;

--echo This must show a trig_cond:
explain extended
select a, oref, 
       a in (select count(*) from t1 group by grp having grp=t2.oref) Z from t2;

drop table t1, t2;

create table t1 (a int, b int, primary key (a));
insert into t1 values (1,1), (3,1),(100,1);
create table t2 (a int, b int);
insert into t2 values (1,1),(2,1),(NULL,1),(NULL,0);

select a,b, a in (select a from t1 where t1.b = t2.b union select a from
t1 where t1.b = t2.b) Z from t2 ;
select a,b, a in (select a from t1 where t1.b = t2.b) Z from t2 ;
drop table t1, t2;


#
# BUG#24127: Incorrect results of row-based subqueries with NULLs on the left side.
#
create table t3 (a int);
insert into t3 values (0),(1),(2),(3),(4),(5),(6),(7),(8),(9);
create table t2 (a int, b int, oref int);
insert into t2 values (NULL,1, 100), (NULL,2, 100);

create table t1 (a int, b int, c int, key(a,b));
insert into t1 select 2*A, 2*A, 100 from t3;

# First test index subquery engine
explain extended select a,b, oref, (a,b) in (select a,b from t1 where c=t2.oref) Z from t2;
select a,b, oref, (a,b) in (select a,b from t1 where c=t2.oref) Z from t2;

# Then check that we do turn off 'ref' scans in the subquery
create table t4 (x int);
insert into t4 select A.a + 10*B.a from t1 A, t1 B;
explain extended 
  select a,b, oref, 
         (a,b) in (select a,b from t1,t4 where c=t2.oref) Z 
  from t2;
select a,b, oref, 
       (a,b) in (select a,b from t1,t4 where c=t2.oref) Z 
from t2;

drop table t1,t2,t3,t4;

--echo # More tests for tricky multi-column cases, where some of pushed-down
--echo # equalities are used for index lookups and some are not.

create table t1 (oref char(4), grp int, ie1 int, ie2 int);
insert into t1 (oref, grp, ie1, ie2) values
 ('aa', 10, 2, 1),
 ('aa', 10, 1, 1),
 ('aa', 20, 2, 1),
 ('bb', 10, 3, 1),
 ('cc', 10, 4, 2),
 ('cc', 20, 3, 2),
 
 ('ee', 10, 2, 1),
 ('ee', 10, 1, 2),
 
 ('ff', 20, 2, 2),
 ('ff', 20, 1, 2);
create table t2 (oref char(4), a int, b int);
insert into t2 values 
  ('ee', NULL, 1),
  ('bb', 2, 1),
  ('ff', 2, 2),
  ('cc', 3, NULL),
  ('bb', NULL, NULL),
  ('aa', 1, 1),
  ('dd', 1, NULL);
alter table t1 add index idx(ie1,ie2);

# cc 3 NULL NULL
select oref, a, b, (a,b) in (select ie1,ie2 from t1 where oref=t2.oref) Z from t2 where a=3 and b is null ;
insert into t2 values ('new1', 10,10);
insert into t1 values ('new1', 1234, 10, NULL); 
# new1, 10, 10, NULL,
select oref, a, b, (a,b) in (select ie1,ie2 from t1 where oref=t2.oref) Z from t2 where a=10 and b=10; 
explain extended
select oref, a, b, (a,b) in (select ie1,ie2 from t1 where oref=t2.oref) Z from t2 where a=10 and b=10; 
drop table t1, t2;

# Now test different column types:
create table t1 (oref char(4), grp int, ie int);
insert into t1 (oref, grp, ie) values
 ('aa', 10, 2),
 ('aa', 10, 1),
 ('aa', 20, NULL),

 ('bb', 10, 3),

 ('cc', 10, 4),
 ('cc', 20, NULL),
 
 ('ee', 10, NULL),
 ('ee', 10, NULL),
 
 ('ff', 20, 2),
 ('ff', 20, 1);

create table t2 (oref char(4), a int);
insert into t2 values 
  ('ee', NULL),
  ('bb', 2),
  ('ff', 2),
  ('cc', 3),
  ('aa', 1),
  ('dd', NULL),
  ('bb', NULL);

select oref, a, a in (select ie from t1 where oref=t2.oref) Z from t2;

select oref, a from t2 where a in (select ie from t1 where oref=t2.oref); 

select oref, a from t2 where a not in (select ie from t1 where oref=t2.oref);


select oref, a, a in (select min(ie) from t1 where oref=t2.oref group by grp) Z from t2;

select oref, a from t2 where 
  a in (select min(ie) from t1 where oref=t2.oref group by grp);
  
select oref, a from t2 where 
  a not in (select min(ie) from t1 where oref=t2.oref group by grp);

#
update t1 set ie=3 where oref='ff' and ie=1;

select oref, a, a in (select min(ie) from t1 where oref=t2.oref group by
grp) Z from t2;


select oref, a from t2 where a in (select min(ie) from t1 where
oref=t2.oref group by grp);

select oref, a from t2 where a not in (select min(ie) from t1 where
oref=t2.oref group by grp);

select oref, a, a in (select min(ie) from t1 where oref=t2.oref group by
grp having min(ie) > 1) Z from t2;

select oref, a from t2 where a in (select min(ie) from t1 where
oref=t2.oref group by grp having min(ie) > 1);
  
select oref, a from t2 where a not in (select min(ie) from t1 where
oref=t2.oref group by grp having min(ie) > 1);

#
alter table t1 add index idx(ie);

explain select oref, a, a in (select ie from t1 where oref=t2.oref) Z from t2;

select oref, a, a in (select ie from t1 where oref=t2.oref) Z from t2;

select oref, a from t2 where a in (select ie from t1 where oref=t2.oref); 

select oref, a from t2 where a not in (select ie from t1 where oref=t2.oref);


alter table t1 drop index idx;
alter table t1 add index idx(oref,ie);

explain select oref, a, a in (select ie from t1 where oref=t2.oref) Z from t2;

select oref, a, a in (select ie from t1 where oref=t2.oref) Z from t2;

select oref, a from t2 where a in (select ie from t1 where oref=t2.oref); 

select oref, a from t2 where a not in (select ie from t1 where oref=t2.oref);

explain 
select oref, a, 
       a in (select min(ie) from t1 where oref=t2.oref 
             group by grp having min(ie) > 1) Z 
from t2;

select oref, a, 
       a in (select min(ie) from t1 where oref=t2.oref 
             group by grp having min(ie) > 1) Z 
from t2;

select oref, a from t2 where a in (select min(ie) from t1 where oref=t2.oref 
                                   group by grp having min(ie) > 1);
  
select oref, a from t2 where a not in (select min(ie) from t1 where oref=t2.oref 
                                       group by grp having min(ie) > 1);

drop table t1,t2;

create table t1 (oref char(4), grp int, ie1 int, ie2 int);
insert into t1 (oref, grp, ie1, ie2) values
 ('aa', 10, 2, 1),
 ('aa', 10, 1, 1),
 ('aa', 20, 2, 1),

 ('bb', 10, 3, 1),

 ('cc', 10, 4, 2),
 ('cc', 20, 3, 2),
 
 ('ee', 10, 2, 1),
 ('ee', 10, 1, 2),
 
 ('ff', 20, 2, 2),
 ('ff', 20, 1, 2);

create table t2 (oref char(4), a int, b int);
insert into t2 values 
  ('ee', NULL, 1),
  ('bb', 2, 1),
  ('ff', 2, 2),
  ('cc', 3, NULL),
  ('bb', NULL, NULL),
  ('aa', 1, 1),
  ('dd', 1, NULL);

select oref, a, b, (a,b) in (select ie1,ie2 from t1 where oref=t2.oref) Z from t2;

select oref, a, b from t2 where (a,b) in (select ie1,ie2 from t1 where oref=t2.oref); 

select oref, a, b from t2 where (a,b) not in (select ie1,ie2 from t1 where oref=t2.oref);

select oref, a, b, 
             (a,b) in (select min(ie1),max(ie2) from t1 
                       where oref=t2.oref group by grp) Z 
from t2;

select oref, a, b from t2 where 
  (a,b) in (select min(ie1), max(ie2) from t1 where oref=t2.oref group by grp);
  
select oref, a, b from t2 where
  (a,b) not in (select min(ie1), max(ie2) from t1 where oref=t2.oref group by grp);

alter table t1 add index idx(ie1,ie2);

explain select oref, a, b, (a,b) in (select ie1,ie2 from t1 where oref=t2.oref) Z from t2;

select oref, a, b, (a,b) in (select ie1,ie2 from t1 where oref=t2.oref) Z from t2;

select oref, a, b from t2 where (a,b) in (select ie1,ie2 from t1 where oref=t2.oref); 

select oref, a, b from t2 where (a,b) not in (select ie1,ie2 from t1 where oref=t2.oref);

explain extended 
select oref, a, b, (a,b) in (select ie1,ie2 from t1 where oref=t2.oref) Z from t2;

drop table t1,t2;

create table t1 (oref char(4), grp int, ie int primary key);
insert into t1 (oref, grp, ie) values
 ('aa', 10, 2),
 ('aa', 10, 1),

 ('bb', 10, 3),

 ('cc', 10, 4),
 ('cc', 20, 5),
 ('cc', 10, 6);
 
create table t2 (oref char(4), a int);
insert into t2 values 
  ('ee', NULL),
  ('bb', 2),
  ('cc', 5),
  ('cc', 2),
  ('cc', NULL),
  ('aa', 1),
  ('bb', NULL);

explain select oref, a, a in (select ie from t1 where oref=t2.oref) Z from t2;

select oref, a, a in (select ie from t1 where oref=t2.oref) Z from t2;

explain select oref, a from t2 where a in (select ie from t1 where oref=t2.oref);

select oref, a from t2 where a in (select ie from t1 where oref=t2.oref);

explain select oref, a from t2 where a not in (select ie from t1 where oref=t2.oref);
select oref, a from t2 where a not in (select ie from t1 where oref=t2.oref);

explain 
select oref, a, a in (select min(ie) from t1 where oref=t2.oref group by grp) Z from t2;

select oref, a, a in (select min(ie) from t1 where oref=t2.oref group by grp) Z from t2;

drop table t1,t2;

#
# BUG#24420: row-based IN suqueries with aggregation when the left operand
#            of the subquery predicate may contain NULL values
#

create table t1 (a int, b int);
insert into t1 values (0,0), (2,2), (3,3);
create table t2 (a int, b int);
insert into t2 values (1,1), (3,3);

select a, b, (a,b) in (select a, min(b) from t2 group by a) Z from t1;

insert into t2 values (NULL,4);
select a, b, (a,b) in (select a, min(b) from t2 group by a) Z from t1;

drop table t1,t2;

#
# Bug #24484: Aggregate function used in column list subquery gives erroneous 
# error
#
CREATE TABLE t1 (a int, b INT, c CHAR(10) NOT NULL, PRIMARY KEY (a, b));
INSERT INTO t1 VALUES (1,1,'a'), (1,2,'b'), (1,3,'c'), (1,4,'d'), (1,5,'e'),
  (2,1,'f'), (2,2,'g'), (2,3,'h'), (3,4,'i'),(3,3,'j'), (3,2,'k'), (3,1,'l'),
  (1,9,'m');
CREATE TABLE t2 (a int, b INT, c CHAR(10) NOT NULL, PRIMARY KEY (a, b));
INSERT INTO t2 SELECT * FROM t1;

# Gives error, but should work since it is (a, b) is the PK so only one 
# given match possible
SELECT a, MAX(b), (SELECT t.c FROM t1 AS t WHERE t1.a=t.a AND t.b=MAX(t1.b))
  as test FROM t1 GROUP BY a;
SELECT * FROM t1 GROUP by t1.a
  HAVING (MAX(t1.b) > (SELECT MAX(t2.b) FROM t2 WHERE t2.c < t1.c
                                                HAVING MAX(t2.b+t1.a) < 10));

SELECT a,b,c FROM t1 WHERE b in (9,3,4) ORDER BY b,c;

SELECT a, MAX(b),
 (SELECT COUNT(DISTINCT t.c) FROM t1 AS t WHERE t1.a=t.a AND t.b=MAX(t1.b) 
  LIMIT 1) 
  as cnt, 
 (SELECT t.b FROM t1 AS t WHERE t1.a=t.a AND t.b=MAX(t1.b) LIMIT 1) 
  as t_b,
 (SELECT t.c FROM t1 AS t WHERE t1.a=t.a AND t.b=MAX(t1.b) LIMIT 1) 
  as t_b,
 (SELECT t.c FROM t1 AS t WHERE t1.a=t.a AND t.b=MAX(t1.b) ORDER BY t.c LIMIT 1)
  as t_b
 FROM t1 GROUP BY a;

SELECT a, MAX(b),
 (SELECT t.c FROM t1 AS t WHERE t1.a=t.a AND t.b=MAX(t1.b) LIMIT 1) as test 
 FROM t1 GROUP BY a;


DROP TABLE t1, t2;

# The next three test cases must be executed with the IN=>EXISTS strategy
set @@optimizer_switch="partial_match_rowid_merge=off,partial_match_table_scan=off";

#
# Bug #27870: crash of an equijoin query with WHERE condition containing 
#             a subquery predicate of the form <join attr> NOT IN (SELECT ...)
#

CREATE TABLE t1 (a int);
CREATE TABLE t2 (b int, PRIMARY KEY(b));
INSERT INTO t1 VALUES (1), (NULL), (4);
INSERT INTO t2 VALUES (3), (1),(2), (5), (4), (7), (6);

EXPLAIN EXTENDED 
SELECT a FROM t1, t2 WHERE a=b AND (b NOT IN (SELECT a FROM t1));
SELECT a FROM t1, t2 WHERE a=b AND (b NOT IN (SELECT a FROM t1));
SELECT a FROM t1, t2 WHERE a=b AND (b NOT IN (SELECT a FROM t1 WHERE a > 4));

DROP TABLE t1,t2;

#
# Bug #28375: crash for NOT IN subquery predicate when left operand becomes NULL  
#

CREATE TABLE t1 (id int);
CREATE TABLE t2 (id int PRIMARY KEY);
CREATE TABLE t3 (id int PRIMARY KEY, name varchar(10));
INSERT INTO t1 VALUES (2), (NULL), (3), (1);
INSERT INTO t2 VALUES (234), (345), (457);
INSERT INTO t3 VALUES (222,'bbb'), (333,'ccc'), (111,'aaa');

EXPLAIN
SELECT * FROM t1
  WHERE t1.id NOT IN (SELECT t2.id FROM t2,t3 
                        WHERE t3.name='xxx' AND t2.id=t3.id);
SELECT * FROM t1
  WHERE t1.id NOT IN (SELECT t2.id FROM t2,t3 
                        WHERE t3.name='xxx' AND t2.id=t3.id);

SELECT (t1.id IN (SELECT t2.id FROM t2,t3 
                    WHERE t3.name='xxx' AND t2.id=t3.id)) AS x
  FROM t1;

DROP TABLE t1,t2,t3;   

#
# Bug #22855: Optimizer doesn't rewrite NOT IN subselects to a correlated 
# subquery 
#
CREATE TABLE t1 (a INT NOT NULL);
INSERT INTO t1 VALUES (1),(-1), (65),(66);

CREATE TABLE t2 (a INT UNSIGNED NOT NULL PRIMARY KEY);
INSERT INTO t2 VALUES (65),(66);

SELECT a FROM t1 WHERE a NOT IN (65,66);
SELECT a FROM t1 WHERE a NOT IN (SELECT a FROM t2);
EXPLAIN SELECT a FROM t1 WHERE a NOT IN (SELECT a FROM t2);

DROP TABLE t1, t2;

set @@optimizer_switch=@save_optimizer_switch;

#
# Bug #34763: item_subselect.cc:1235:Item_in_subselect::row_value_transformer:
#             Assertion failed, unexpected error message:
#             ERROR 1247 (42S22): Reference '<list ref>' not supported (forward
#             reference in item list)
#
CREATE TABLE t1 (a INT);
INSERT INTO t1 VALUES(1);

CREATE TABLE t2 (placeholder CHAR(11));
INSERT INTO t2 VALUES("placeholder");

SELECT ROW(1, 2) IN (SELECT t1.a, 2)         FROM t1 GROUP BY t1.a;
SELECT ROW(1, 2) IN (SELECT t1.a, 2 FROM t2) FROM t1 GROUP BY t1.a;

DROP TABLE t1, t2;

#
# Bug #36005: crash in subselect with single row
#             (subselect_single_select_engine::exec)
#

CREATE TABLE t1 (a INT);
INSERT INTO t1 VALUES (1),(2),(3);
CREATE TABLE t2 SELECT * FROM t1;

SELECT 1 FROM t1 WHERE t1.a NOT IN (SELECT 1 FROM t1, t2 WHERE 0);

DROP TABLE t1, t2;

#
# Bug 2198
#

create table t1 (a int, b decimal(13, 3)); 
insert into t1 values (1, 0.123);
--disable_ps2_protocol
select a, (select max(b) from t1) into outfile "../../tmp/subselect.out.file.1" from t1;
--enable_ps2_protocol
delete from t1;
load data infile "../../tmp/subselect.out.file.1" into table t1;
select * from t1;
drop table t1;
let $datadir=`select @@datadir`;
--remove_file $MYSQLTEST_VARDIR/tmp/subselect.out.file.1

#
# Bug #37894: Assertion in init_read_record_seq in handler.h line 1444
#

CREATE TABLE t1 (
  pk INT PRIMARY KEY,
  int_key INT,
  varchar_key VARCHAR(5) UNIQUE,
  varchar_nokey VARCHAR(5)
);
INSERT INTO t1 VALUES (9, 7,NULL,NULL), (10,8,'p' ,'p');

SELECT varchar_nokey
FROM t1
WHERE NULL NOT IN (
 SELECT INNR.pk FROM t1 AS INNR2
   LEFT JOIN t1 AS INNR ON ( INNR2.int_key = INNR.int_key )
   WHERE INNR.varchar_key > 'n{'
);

DROP TABLE t1;

#
# Bug #39069: <row constructor> IN <table-subquery> seriously messed up
#

CREATE TABLE t1 (a INT);
INSERT INTO t1 VALUES (1), (2), (11);

--echo # 2nd and 3rd columns should be same
SELECT a, ROW(11, 12) = (SELECT a, 22), ROW(11, 12) IN (SELECT a, 22) FROM t1 GROUP BY t1.a;
SELECT a, ROW(11, 12) = (SELECT a, 12), ROW(11, 12) IN (SELECT a, 12) FROM t1 GROUP BY t1.a;
SELECT a, ROW(11, 12) = (SELECT a, 22), ROW(11, 12) IN (SELECT a, 22) FROM t1;
SELECT a, ROW(11, 12) = (SELECT a, 12), ROW(11, 12) IN (SELECT a, 12) FROM t1;

# The x alias is used below to workaround bug #40674.
# Regression tests for sum function on outer column in subselect from dual:
SELECT a AS x, ROW(11, 12) = (SELECT MAX(x), 22), ROW(11, 12) IN (SELECT MAX(x), 22) FROM t1;
--echo # 2nd and 3rd columns should be same
EXPLAIN SELECT a AS x, ROW(11, 12) = (SELECT MAX(x), 12), ROW(11, 12) IN (SELECT MAX(x), 12) FROM t1;
SELECT a AS x, ROW(11, 12) = (SELECT MAX(x), 12), ROW(11, 12) IN (SELECT MAX(x), 12) FROM t1;

DROP TABLE t1;

#enable after fix MDEV-28585
--disable_view_protocol
--echo # both columns should be same
SELECT ROW(1,2) = (SELECT NULL, NULL), ROW(1,2) IN (SELECT NULL, NULL);
SELECT ROW(1,2) = (SELECT   1,  NULL), ROW(1,2) IN (SELECT    1, NULL);
SELECT ROW(1,2) = (SELECT NULL,    2), ROW(1,2) IN (SELECT NULL,    2);
SELECT ROW(1,2) = (SELECT NULL,    1), ROW(1,2) IN (SELECT NULL,    1);
SELECT ROW(1,2) = (SELECT    1,    1), ROW(1,2) IN (SELECT    1,    1);
SELECT ROW(1,2) = (SELECT    1,    2), ROW(1,2) IN (SELECT    1,    2);
--enable_view_protocol

#
# Bug #37362      Crash in do_field_eq
#
# Note that currently this test produces wrong output, see MBug#430669.
#
CREATE TABLE t1 (a INT, b INT, c INT);
INSERT INTO t1 VALUES (1,1,1), (1,1,1);

--error 1054
EXPLAIN EXTENDED 
  SELECT c FROM 
    ( SELECT 
      (SELECT COUNT(a) FROM 
        (SELECT COUNT(b) FROM t1) AS x GROUP BY c
      ) FROM t1 GROUP BY b
    ) AS y;
SHOW WARNINGS;

DROP TABLE t1;


--echo End of 5.0 tests

--echo #  
--echo # BUG#36896: Server crash on SELECT FROM DUAL
--echo # 
create table t1 (a int);
select 1 as res from dual where (1) in (select * from t1);
drop table t1;

#
# BUG#36135 "void Diagnostics_area::set_eof_status(THD*): Assertion `!is_set()' failed."
#
create table t0 (a int);
insert into t0 values (0),(1),(2),(3),(4),(5),(6),(7),(8),(9);

create table t1 (
  a int(11) default null,
  b int(11) default null,
  key (a)
);
# produce numbers 0..999
insert into t1 select A.a+10*(B.a+10*C.a),A.a+10*(B.a+10*C.a) from t0 A, t0 B, t0 C;

create table t2 (a int(11) default null);
insert into t2 values (0),(1);

create table t3 (a int(11) default null);
insert into t3 values (0),(1);

create table t4 (a int(11) default null);
insert into t4 values (0),(1);

create table t5 (a int(11) default null);
insert into t5 values (0),(1),(0),(1);

# this must not fail assertion
--error 1242
select * from t2, t3 
where
    t2.a < 10 and
    t3.a+1 = 2 and
    t3.a in (select t1.b from t1
                 where t1.a+1=t1.a+1 and
                       t1.a < (select t4.a+10                                  
                                  from t4, t5 limit 2));

drop table t0, t1, t2, t3, t4, t5;

--echo # 
--echo # BUG#48177 - SELECTs with NOT IN subqueries containing NULL 
--echo #             values return too many records
--echo # 

CREATE TABLE t1 (
  i1 int DEFAULT NULL,
  i2 int DEFAULT NULL
) ;

INSERT INTO t1 VALUES (1,    NULL);
INSERT INTO t1 VALUES (2,    3);
INSERT INTO t1 VALUES (4,    NULL);
INSERT INTO t1 VALUES (4,    0);
INSERT INTO t1 VALUES (NULL, NULL);

CREATE TABLE t2 (
  i1 int DEFAULT NULL,
  i2 int DEFAULT NULL
) ;

INSERT INTO t2 VALUES (4, NULL);
INSERT INTO t2 VALUES (5, 0);

--echo 
--echo Data in t1
SELECT i1, i2 FROM t1;

--echo 
--echo Data in subquery (should be filtered out)
SELECT i1, i2 FROM t2 ORDER BY i1;

FLUSH STATUS;
set @save_optimizer_switch2=@@optimizer_switch;
set optimizer_switch='subquery_cache=off';

--disable_ps2_protocol
--echo 
SELECT i1, i2
FROM t1
WHERE (i1, i2) 
      NOT IN (SELECT i1, i2 FROM t2);

--echo 
--echo # Check that the subquery only has to be evaluated once 
--echo # for all-NULL values even though there are two (NULL,NULL) records
--echo # Baseline:
SHOW STATUS LIKE '%Handler_read_rnd_next';

--echo 
INSERT INTO t1 VALUES (NULL, NULL); 
FLUSH STATUS;

--echo 
SELECT i1, i2
FROM t1
WHERE (i1, i2) 
      NOT IN (SELECT i1, i2 FROM t2);

--echo 
--echo # Handler_read_rnd_next should be one more than baseline 
--echo # (read record from t1, but do not read from t2)
SHOW STATUS LIKE '%Handler_read_rnd_next';
--enable_ps2_protocol

set @@optimizer_switch=@save_optimizer_switch2;
DROP TABLE t1,t2;

--echo End of 5.1 tests

#
# Test for the problem with using sj-materialization when subquery's select 
# list element SCOL is covered by equality propagation and has preceding equal
# column PCOL which belongs to a table within the the semi-join nest: SJM-Scan
# process should unpack column value not to SCOL but rather to PCOL, as 
# substitute_best_equal has made all conditions to refer to PCOL.
#
CREATE TABLE t1 (
  a int(11) NOT NULL,
  b int(11) NOT NULL,
  c datetime default NULL,
  PRIMARY KEY  (a),
  KEY idx_bc (b,c)
);

INSERT INTO t1 VALUES 
(406989,67,'2006-02-23 17:08:46'), (150078,67,'2005-10-26 11:17:45'),
(406993,67,'2006-02-27 11:20:57'), (245655,67,'2005-12-08 15:59:08'),
(406994,67,'2006-02-27 11:26:46'), (256,67,NULL),
(398341,67,'2006-02-20 04:48:44'), (254,67,NULL),(1120,67,NULL),
(406988,67,'2006-02-23 17:07:22'), (255,67,NULL),
(398340,67,'2006-02-20 04:38:53'),(406631,67,'2006-02-23 10:49:42'),
(245653,67,'2005-12-08 15:59:07'),(406992,67,'2006-02-24 16:47:18'),
(245654,67,'2005-12-08 15:59:08'),(406995,67,'2006-02-28 11:55:00'),
(127261,67,'2005-10-13 12:17:58'),(406991,67,'2006-02-24 16:42:32'),
(245652,67,'2005-12-08 15:58:27'),(398545,67,'2006-02-20 04:53:13'),
(154504,67,'2005-10-28 11:53:01'),(9199,67,NULL),(1,67,'2006-02-23 15:01:35'),
(223456,67,NULL),(4101,67,NULL),(1133,67,NULL),
(406990,67,'2006-02-23 18:01:45'),(148815,67,'2005-10-25 15:34:17'),
(148812,67,'2005-10-25 15:30:01'),(245651,67,'2005-12-08 15:58:27'),
(154503,67,'2005-10-28 11:52:38');

create table t11 select * from t1 where b = 67 AND (c IS NULL OR c > NOW()) order by 3 asc;
create table t12 select * from t1 where b = 67 AND (c IS NULL OR c > NOW()) order by 3 desc;
create table t21 select * from t1 where b = 67 AND (c IS NULL OR c > '2005-12-08') order by 3 asc;
create table t22 select * from t1 where b = 67 AND (c IS NULL OR c > '2005-12-08') order by 3 desc;

set @@optimizer_switch=@save_optimizer_switch;
set @@optimizer_switch='materialization=off';

update t22 set c = '2005-12-08 15:58:27' where a = 255;
explain select t21.* from t21,t22 where t21.a = t22.a and 
t22.a in (select t12.a from t11, t12 where t11.a in(255,256) and t11.a = t12.a and t11.c is null) and t22.c is null order by t21.a;
select t21.* from t21,t22 where t21.a = t22.a and 
t22.a in (select t12.a from t11, t12 where t11.a in(255,256) and t11.a = t12.a and t11.c is null) and t22.c is null order by t21.a;

set @@optimizer_switch=@save_optimizer_switch;

drop table t1, t11, t12, t21, t22;

#
# Test sj-materialization re-execution. The test isn't meaningful (materialized
# table stays the same across all executions) because it's hard to create a
# dataset that would verify correct re-execution without hitting BUG#31480
# 
create table t1(a int);
insert into t1 values (0),(1);

set @@optimizer_switch='firstmatch=off,materialization=off';
explain 
select (select max(Y.a) from t1 Y where a in (select a from t1 Z) and a < X.a) as subq from t1 X;
select (select max(Y.a) from t1 Y where a in (select a from t1 Z) and a < X.a) as subq from t1 X;
set @@optimizer_switch=@save_optimizer_switch;

drop table t1;

#
# Test confluent duplicate weedout
#
create table t0 (a int);
insert into t0 values (0),(1),(2),(3),(4),(5),(6),(7),(8),(9);
create table t1 as select * from t0;
insert into t1 select a+10 from t0;
set @@optimizer_switch='firstmatch=off,materialization=off';
insert into t0 values(2);
explain select * from t1 where 2 in (select a from t0);
select * from t1 where 2 in (select a from t0);
set @@optimizer_switch=@save_optimizer_switch;
set @@optimizer_switch='materialization=off';
explain select * from t1 where 2 in (select a from t0);
select * from t1 where 2 in (select a from t0);
set @@optimizer_switch=@save_optimizer_switch;


set @@optimizer_switch=@save_optimizer_switch;
set @@optimizer_switch='materialization=off';

# 
# FirstMatch referring to a derived table
#
set @tmp_optimizer_switch=@@optimizer_switch;
set optimizer_switch='derived_merge=off,derived_with_keys=off';
explain select * from (select a from t0) X where a in (select a from t1);
drop table t0, t1;
set optimizer_switch=@tmp_optimizer_switch;

#
# LooseScan: Check if we can pick it together with range access
#
create table t0 (a int);
insert into t0 values (0),(1),(2),(3),(4),(5),(6),(7),(8),(9);

create table t1 (kp1 int, kp2 int, c int, filler char(100), key(kp1, kp2));
insert into t1 select A.a+10*(B.a+10*C.a), 0, 0, 'filler' from t0 A, t0 B, t0 C;
insert into t1 select * from t1 where kp1 < 20;

create table t3 (a int);
insert into t3 select A.a + 10*B.a from t0 A, t0 B;

explain select * from t3 where a in (select kp1 from t1 where kp1<20);

create table t4 (pk int primary key);
insert into t4 select a from t3;

explain select * from t3 where a in (select t1.kp1 from t1,t4 where kp1<20
and t4.pk=t1.c);

drop table t1, t3, t4;

set @@optimizer_switch=@save_optimizer_switch;

#
# Test if we handle duplicate elimination temptable overflowing to disk
#
create table t1 (a int) as select * from t0 where a < 5;

set @save_max_heap_table_size=@@max_heap_table_size;
set @@optimizer_switch='firstmatch=off,materialization=off';
set @@max_heap_table_size= 16384;

explain select count(*) from t0 A, t0 B, t0 C, t0 D where D.a in (select a from t1 E where a+1 < 10000 + A.a + B.a +C.a+D.a);
flush status;
--disable_ps2_protocol
select count(*) from t0 A, t0 B, t0 C, t0 D where D.a in (select a from t1 E where a+1 < 10000 + A.a + B.a +C.a+D.a);
--enable_ps2_protocol
show status like 'Created_tmp_disk_tables';
set @@max_heap_table_size=@save_max_heap_table_size;
set @@optimizer_switch=@save_optimizer_switch;
drop table t0, t1;

#
# Materialize + Scan + ref access to the subsequent table based on scanned
# value
# 
create table t0 (a int);
insert into t0 values (0),(1),(2),(3),(4),(5),(6),(7),(8),(9);
create table t2(a int);
insert into t2 values (1),(2);
create table t3 ( a int , filler char(100), key(a)); 
insert into t3 select A.a + 10*B.a, 'filler' from t0 A, t0 B;
explain select * from t3 where a in (select a from t2) and (a > 5 or a < 10); 
select * from t3 where a in (select a from t2); 

drop table t0, t2, t3;

#
# DATETIME type checks
#
set @@optimizer_switch='firstmatch=off,materialization=off';
create table t1 (a date);
insert into t1 values ('2008-01-01'),('2008-01-01'),('2008-02-01'),('2008-02-01');
create table t2 (a int);
insert into t2 values (1),(2);
create table t3 (a char(10));
insert into t3 select * from t1;
insert into t3 values (1),(2);
explain select * from t2 where a in (select a from t1);
explain select * from t2 where a in (select a from t2);
explain select * from t2 where a in (select a from t3);
explain select * from t1 where a in (select a from t3);
drop table t1, t2, t3;
create table t1 (a decimal);
insert into t1 values (1),(2);
explain select * from t1 where a in (select a from t1);
drop table t1;
set @@optimizer_switch=@save_optimizer_switch;

set @@optimizer_switch='materialization=off';

#
# SJ-Materialization-scan for non-first table
#
create table t1 (a int);
insert into t1 values (0),(1),(2),(3),(4),(5),(6),(7),(8),(9);
create table t2 as select * from t1;
create table t3 (a int, b int, filler char(100), key(a));
insert into t3 select A.a + 10*B.a, A.a + 10*B.a, 'filler' from t1 A, t1 B, t1 C;
explain select * from t1, t3 where t3.a in (select a from t2) and (t3.a < 10 or t3.a >30) and t1.a =3;

#
# Verify that straight_join modifier in parent or child prevents flattening
#
explain select straight_join * from t1 A, t1 B where A.a in (select a from t2);
explain select * from t2 where a in (select straight_join A.a from t1 A, t1 B);
explain select * from t2 where a in (select straight_join A.a from t1 A, t1 B);
explain select straight_join * from t2 X, t2 Y 
where X.a in (select straight_join A.a from t1 A, t1 B);

--echo #
--echo # SJ-Materialization scan + first table being system const table
--echo #
create table t0 (a int, b int);
insert into t0 values(1,1);
explain select * from t0, t3 where t3.a in (select a from t2) and (t3.a < 10 or t3.a >30);
create table t4 as select a as x, a as y from t1;
explain select * from t0, t3 where (t3.a, t3.b) in (select x,y from t4) and (t3.a < 10 or t3.a >30);
drop table t0,t1,t2,t3,t4;

--echo #
--echo # LooseScan with ref access
--echo #

set @@optimizer_switch='join_cache_hashed=off';

create table t0 (a int);
insert into t0 values (0),(1),(2),(3),(4),(5),(6),(7),(8),(9);
create table t1 (a int, b int, filler char(100), key(a,b));
insert into t1 select A.a, B.a, 'filler' from t0 A, t0 B;
create table t2 as select * from t1;

explain select * from t2 where a in (select b from t1 where a=3);
explain select * from t2 where (b,a) in (select a,b from t1 where a=3);

drop table t1,t2;

set @@optimizer_switch=@save_optimizer_switch;

#
# Multi-column sj-materialization with lookups
#
create table t1 (a int, b int);
insert into t1 select a,a from t0;
create table t2 (a int, b int); 
insert into t2 select A.a + 10*B.a, A.a + 10*B.a from t0 A, t0 B;

set @@optimizer_switch='firstmatch=off,materialization=off';
explain select * from t1 where (a,b) in (select a,b from t2);

# A smallish test if find_best() still works for semi-join optimization:
set @save_optimizer_search_depth=@@optimizer_search_depth;
set @@optimizer_search_depth=63;
explain select * from t1 where (a,b) in (select a,b from t2);
set @@optimizer_search_depth=@save_optimizer_search_depth;
set @@optimizer_switch=@save_optimizer_switch;

drop table t0, t1, t2;

set @@optimizer_switch='materialization=off';

--echo #
--echo # Primitive SJ-Materialization tests for DECIMAL and DATE
--echo #
create table t0 (a decimal(4,2));
insert into t0 values (10.24), (22.11);
create table t1 as select * from t0;
insert into t1 select * from t0;
explain select * from t0 where a in (select a from t1);
select * from t0 where a in (select a from t1);
drop table t0, t1;

create table t0(a date);
insert into t0 values ('2008-01-01'),('2008-02-02');
create table t1 as select * from t0;
insert into t1 select * from t0;
explain select * from t0 where a in (select a from t1);
select * from t0 where a in (select a from t1);
drop table t0, t1;

#
# Fix a trivial crash with SJ-Materialization lookup, multiple tables in the
# subquery, and a condition on some of inner tables but not others
#
create table t0(a int);
insert into t0 values (0),(1),(2),(3),(4),(5),(6),(7),(8),(9);
create table t1 as select a as a, a as b, a as c from t0 where a < 3;
create table t2 as select a as a, a as b from t0 where a < 3;
insert into t2 select * from t2;
select count(*) from t2;

explain select * from t1 where (a,b,c) in (select X.a, Y.a, Z.a from t2 X, t2 Y, t2 Z where X.b=33);

drop table t0,t1,t2;

set @@optimizer_switch=@save_optimizer_switch;

--echo
--echo BUG#37842: Assertion in DsMrr_impl::dsmrr_init, at handler.cc:4307
--echo

CREATE TABLE t1 (
  `pk` int(11) NOT NULL AUTO_INCREMENT,
  `int_key` int(11) DEFAULT NULL,
  PRIMARY KEY (`pk`),
  KEY `int_key` (`int_key`)
) ENGINE=MyISAM;
INSERT INTO t1 VALUES (1,9),(2,3),(3,8),(4,6),(5,9),(6,5),(7,5),(8,9),(9,1),(10,10);
SELECT `pk` FROM t1 AS OUTR WHERE `int_key` = ALL (
 SELECT `int_key` FROM t1 AS INNR WHERE INNR . `pk` >= 9
);
DROP TABLE t1;

--echo 
--echo BUG#40118 Crash when running Batched Key Access and requiring one match for each key
--echo
create table t0(a int);
insert into t0 values (0),(1),(2),(3),(4),(5),(6),(7),(8),(9);
create table t1 (a int, key(a));
insert into t1 select * from t0;
alter table t1 add b int not null, add filler char(200);
insert into t1 select * from t1;
insert into t1 select * from t1;

set @save_join_cache_level=@@join_cache_level;
set join_cache_level=6;
select * from t0 where t0.a in (select t1.a from t1 where t1.b=0); 
set join_cache_level=@save_join_cache_level;
drop table t0, t1;

--echo # 
--echo # BUG#32665 Query with dependent subquery is too slow
--echo #
create table t1 (
  idIndividual int primary key
);
insert into t1 values (1),(2),(1000);

create table t2 (
  idContact int primary key,
  contactType int,
  idObj int
);
insert into t2 values (1,1,1),(2,2,2),(3,3,3);

create table t3 (
  idAddress int primary key,
  idContact int,
  postalStripped varchar(100)
);

insert into t3 values (1,1, 'foo'), (2,2,'T2H3B2');

--echo The following must be converted to a semi-join:
set @save_optimizer_switch=@@optimizer_switch;
set @@optimizer_switch='materialization=off';
explain extended SELECT a.idIndividual FROM t1 a 
WHERE a.idIndividual IN 
	(	SELECT c.idObj FROM t3 cona 
		INNER JOIN t2 c ON c.idContact=cona.idContact 
		WHERE cona.postalStripped='T2H3B2'
	);
SELECT a.idIndividual FROM t1 a 
WHERE a.idIndividual IN 
	(	SELECT c.idObj FROM t3 cona 
		INNER JOIN t2 c ON c.idContact=cona.idContact 
		WHERE cona.postalStripped='T2H3B2'
	);
set @@optimizer_switch=@save_optimizer_switch;
drop table t1,t2,t3;

--echo #
--echo # BUG#47367 Crash in Name_resolution_context::process_error
--echo #

SET SESSION optimizer_switch = 'semijoin=off';
CREATE TABLE t1 (f1 INTEGER);
CREATE TABLE t2 LIKE t1;
delimiter |;
CREATE PROCEDURE p1 () BEGIN SELECT f1 FROM t1 WHERE f1 IN (SELECT f1 FROM t2); END|
delimiter ;|
CALL p1;
ALTER TABLE t2 CHANGE COLUMN f1 my_column INT;
CALL p1;
DROP PROCEDURE p1;
--echo # Restore the original column list of table t2:
ALTER TABLE t2 CHANGE COLUMN my_column f1 INT;

SET SESSION optimizer_switch = 'semijoin=on';
delimiter |;
--echo # Recreate procedure so that we eliminate any caching effects
CREATE PROCEDURE p1 () BEGIN SELECT f1 FROM t1 WHERE f1 IN (SELECT f1 FROM t2); END|
delimiter ;|
CALL p1;
ALTER TABLE t2 CHANGE COLUMN f1 my_column INT;
CALL p1;
DROP PROCEDURE p1;
DROP TABLE t1, t2;

--echo #
--echo # fix of lp:824425 (prohibiting subqueries in row in
--echo # left part of IN/ALL/ANY)
--echo #

CREATE TABLE t1 ( a int) ;
INSERT INTO t1 VALUES (20),(30);

CREATE TABLE t2 (a int) ;
INSERT INTO t2 VALUES (3),(9);

CREATE TABLE t3 ( a int, b int) ;
INSERT INTO t3 VALUES (20,5),(30,6);

set @optimizer_switch_save=@@optimizer_switch;
SET SESSION optimizer_switch='semijoin=OFF,in_to_exists=OFF,materialization=ON,partial_match_rowid_merge=ON,partial_match_table_scan=OFF';

SELECT * FROM t1
WHERE (
        ( SELECT a FROM t2 WHERE a = 9 )
) NOT IN (
        SELECT b
        FROM t3
);
explain extended
SELECT * FROM t1
WHERE (
        ( SELECT a FROM t2 WHERE a = 9 )
) NOT IN (
        SELECT b
        FROM t3
);

--error ER_NOT_SUPPORTED_YET
SELECT * FROM t1
WHERE (
        ( SELECT a FROM t2 WHERE a = 9 ),
        ( SELECT a FROM t2 WHERE a = 3 )
) NOT IN (
        SELECT b , a
        FROM t3
);
set optimizer_switch=@optimizer_switch_save;

drop table t1,t2,t3;

--echo End of 5.3 tests

--echo #
--echo # BUG#48920: COUNT DISTINCT returns 1 for NULL values when in a subquery 
--echo #            in the select list
--echo #

--echo 
CREATE TABLE t1 (
  i int(11) DEFAULT NULL,
  v varchar(1) DEFAULT NULL
);

--echo 
INSERT INTO t1 VALUES (8,'v');
INSERT INTO t1 VALUES (9,'r');
INSERT INTO t1 VALUES (NULL,'y');

--echo 
CREATE TABLE t2 (
  i int(11) DEFAULT NULL,
  v varchar(1) DEFAULT NULL,
  KEY i_key (i)
);

--echo 
INSERT INTO t2 VALUES (NULL,'r');
INSERT INTO t2 VALUES (0,'c');
INSERT INTO t2 VALUES (0,'o');
INSERT INTO t2 VALUES (2,'v');
INSERT INTO t2 VALUES (7,'c');

--echo 
SELECT i, v, (SELECT COUNT(DISTINCT i)
              FROM t1
              WHERE v  = t2.v) as subsel
FROM t2;

--echo 
EXPLAIN EXTENDED
SELECT i, v, (SELECT COUNT(DISTINCT i)
              FROM t1
              WHERE v  = t2.v) as subsel
FROM t2;

DROP TABLE t1,t2;

--echo End of 5.6 tests
