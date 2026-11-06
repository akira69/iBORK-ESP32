// Twin-blade knife switch. Export all poses with the camera fixed on the faceplate.
travel = 0;  // -4 open, 0 mid-travel, +4 closed
angle = (4 - travel) * 11.25;  // At zero degrees both blades sit between the upper jaws.
$fn = 32;
copper = [0.65,0.37,0.18];
brass = [0.86,0.64,0.32];
iron = [0.25,0.27,0.27];

module screw(x,y,z) {
    translate([x,y,z]) rotate([90,0,0]) difference() {
        color(brass) cylinder(h=1.5,r=2.2);
        translate([0,0,1.45]) rotate([0,0,32]) cube([5,0.7,0.8],center=true);
    }
}

color([0.20,0.22,0.23]) translate([-27,0,0]) cube([54,4,78]);
color([0.31,0.33,0.33]) translate([-26,-0.65,1]) cube([52,0.7,76]);
for (x=[-15,15]) {
    // Lower pivot and upper spring contacts stay on the slate base.
    for (z=[12,53]) {
        color(copper) translate([x-5,-3,z-6]) cube([10,3,18]);
        screw(x,-3,z-2);
    }
    for (side=[-1,1]) {
        color(brass) translate([x+side*2.5-0.75,-9,16]) cube([1.5,7,9]);
        color(brass) translate([x+side*2.5-0.75,-14,56]) cube([1.5,12,9]);
    }
    color(iron) translate([x-4.8,-7,20]) rotate([0,90,0]) cylinder(h=9.6,r=2.1);
}
for (x=[-23,23], z=[4,74]) screw(x,-0.7,z);
translate([0,-7,20]) rotate([angle,0,0]) {
    for (x=[-15,15]) {
        color(copper) translate([x-1.75,-2,0]) cube([3.5,4,49]);
        color(brass) translate([x-1.75,-2.05,2]) cube([0.7,0.2,45]);
    }
    color([0.28,0.23,0.19]) translate([-18,-3,47]) cube([36,6,5]);
    for (x=[-15,15]) screw(x,-3,49.5);
    color(brass) translate([0,0,52]) cylinder(h=4,r=4.4);
    color([0.11,0.12,0.13]) translate([0,0,56]) cylinder(h=22,r1=3.5,r2=4.8);
    color([0.19,0.20,0.20]) translate([0,0,78]) cylinder(h=1.6,r1=4.8,r2=4.2);
}
