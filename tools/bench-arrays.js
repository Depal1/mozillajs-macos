var a = [];
for (var i = 0; i < 300000; i++)
    a.push({ x: i });
var sum = 0;
for (var i = 0; i < a.length; i++)
    sum += a[i].x;
a.sort(function (p, q) { return q.x - p.x; });
print(sum);
